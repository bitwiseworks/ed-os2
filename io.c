/* io.c: i/o routines for the ed line editor */
/*  GNU ed - The GNU line editor.
    Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
    Copyright (C) 2006-2019 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef __OS2__
#include <stdlib.h>
#endif

#include "ed.h"


static const line_t * unterminated_line = 0;	/* last line has no '\n' */
int linenum_ = 0;				/* script line number */

void reset_unterminated_line( void ) { unterminated_line = 0; }

void unmark_unterminated_line( const line_t * const lp )
  { if( unterminated_line == lp ) unterminated_line = 0; }

static bool unterminated_last_line( void )
  { return ( unterminated_line != 0 &&
             unterminated_line == search_line_node( last_addr() ) ); }

int linenum( void ) { return linenum_; }

#ifdef __OS2__
static bool textmode = false;
void set_textmode( void ) { textmode = true; }
#endif

/* print text to stdout */
static void print_line( const char * p, int len, const int pflags )
  {
  const char escapes[] = "\a\b\f\n\r\t\v";
  const char escchars[] = "abfnrtv";
  int col = 0;

  if( pflags & GNP ) { printf( "%d\t", current_addr() ); col = 8; }
  while( --len >= 0 )
    {
    const unsigned char ch = *p++;
    if( !( pflags & GLS ) ) putchar( ch );
    else
      {
      if( ++col > window_columns() ) { col = 1; fputs( "\\\n", stdout ); }
      if( ch >= 32 && ch <= 126 )
        { if( ch == '$' || ch == '\\' ) { ++col; putchar('\\'); }
          putchar( ch ); }
      else
        {
        char * const p = strchr( escapes, ch );
        ++col; putchar('\\');
        if( ch && p ) putchar( escchars[p-escapes] );
        else
          {
          col += 2;
          putchar( ( ( ch >> 6 ) & 7 ) + '0' );
          putchar( ( ( ch >> 3 ) & 7 ) + '0' );
          putchar( ( ch & 7 ) + '0' );
          }
        }
      }
    }
  if( !traditional() && ( pflags & GLS ) ) putchar('$');
  putchar('\n');
  }


/* print a range of lines to stdout */
bool print_lines( int from, const int to, const int pflags )
  {
  line_t * const ep = search_line_node( inc_addr( to ) );
  line_t * bp = search_line_node( from );

  if( !from ) { set_error_msg( "Invalid address" ); return false; }
  while( bp != ep )
    {
    const char * const s = get_sbuf_line( bp );
    if( !s ) return false;
    set_current_addr( from++ );
    print_line( s, bp->len, pflags );
    bp = bp->q_forw;
    }
  return true;
  }


/* return the parity of escapes at the end of a string */
static bool trailing_escape( const char * const s, int len )
  {
  bool odd_escape = false;
  while( --len >= 0 && s[len] == '\\' ) odd_escape = !odd_escape;
  return odd_escape;
  }


/* If *ibufpp contains an escaped newline, get an extended line (one
   with escaped newlines) from stdin.
   The backslashes escaping the newlines are stripped.
   Return line length in *lenp, including the trailing newline. */
bool get_extended_line( const char ** const ibufpp, int * const lenp,
                        const bool strip_escaped_newlines )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int len;

  for( len = 0; (*ibufpp)[len++] != '\n'; ) ;
  if( len < 2 || !trailing_escape( *ibufpp, len - 1 ) )
    { if( lenp ) *lenp = len; return true; }
  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return false;
  memcpy( buf, *ibufpp, len );
  --len; buf[len-1] = '\n';			/* strip trailing esc */
  if( strip_escaped_newlines ) --len;		/* strip newline */
  while( true )
    {
    int len2;
    const char * const s = get_stdin_line( &len2 );
    if( !s ) return false;			/* error */
    if( len2 <= 0 ) return false;		/* EOF */
    if( !resize_buffer( &buf, &bufsz, len + len2 + 1 ) ) return false;
    memcpy( buf + len, s, len2 );
    len += len2;
    if( len2 < 2 || !trailing_escape( buf, len - 1 ) ) break;
    --len; buf[len-1] = '\n';			/* strip trailing esc */
    if( strip_escaped_newlines ) --len;		/* strip newline */
    }
  buf[len] = 0;
  *ibufpp = buf;
  if( lenp ) *lenp = len;
  return true;
  }


/* Read a line of text from stdin.
   Incomplete lines (lacking the trailing newline) are discarded.
   Returns pointer to buffer and line size (including trailing newline),
   or 0 if error, or *sizep = 0 if EOF */
const char * get_stdin_line( int * const sizep )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int i = 0;

  while( true )
    {
    const int c = getchar();
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) { *sizep = 0; return 0; }
    if( c == EOF )
      {
      if( ferror( stdin ) )
        {
        show_strerror( "stdin", errno );
        set_error_msg( "Cannot read stdin" );
        clearerr( stdin );
        *sizep = 0; return 0;
        }
      if( feof( stdin ) )
        {
        set_error_msg( "Unexpected end-of-file" );
        clearerr( stdin );
        buf[0] = 0; *sizep = 0; if( i > 0 ) ++linenum_;	/* discard line */
        return buf;
        }
      }
    else
      {
      buf[i++] = c; if( !c ) set_binary(); if( c != '\n' ) continue;
      ++linenum_; buf[i] = 0; *sizep = i;
      return buf;
      }
    }
  }


/* Read a line of text from a stream.
   Returns pointer to buffer and line size (including trailing newline
   if it exists and is not added now) */
static const char * read_stream_line( FILE * const fp, int * const sizep,
                                      bool * const newline_addedp )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int c, i = 0;

  while( true )
    {
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) return 0;
    c = getc( fp ); if( c == EOF ) break;
    buf[i++] = c;
    if( !c ) set_binary(); else if( c == '\n' ) break;
    }
  buf[i] = 0;
  if( c == EOF )
    {
    if( ferror( fp ) )
      {
      show_strerror( 0, errno );
      set_error_msg( "Cannot read input file" );
      return 0;
      }
    else if( i )
      {
      buf[i] = '\n'; buf[i+1] = 0; *newline_addedp = true;
      if( !isbinary() ) ++i;
      }
    }
  *sizep = i;
  return buf;
  }


/* read a stream into the editor buffer;
   return total size of data read, or -1 if error */
static long read_stream( FILE * const fp, const int addr )
  {
  line_t * lp = search_line_node( addr );
  undo_t * up = 0;
  long total_size = 0;
  const bool o_isbinary = isbinary();
  const bool appended = ( addr == last_addr() );
  const bool o_unterminated_last_line = unterminated_last_line();
  bool newline_added = false;

  set_current_addr( addr );
  while( true )
    {
    int size = 0;
    const char * const s = read_stream_line( fp, &size, &newline_added );
    if( !s ) return -1;
    if( size <= 0 ) break;
    total_size += size;
    disable_interrupts();
    if( !put_sbuf_line( s, size + newline_added ) )
      { enable_interrupts(); return -1; }
    lp = lp->q_forw;
    if( up ) up->tail = lp;
    else
      {
      up = push_undo_atom( UADD, current_addr(), current_addr() );
      if( !up ) { enable_interrupts(); return -1; }
      }
    enable_interrupts();
    }
  if( addr && appended && total_size && o_unterminated_last_line )
    fputs( "Newline inserted\n", stdout );		/* before stream */
  else if( newline_added && ( !appended || !isbinary() ) )
    fputs( "Newline appended\n", stdout );		/* after stream */
  if( !appended && isbinary() && !o_isbinary && newline_added )
    ++total_size;
  if( appended && isbinary() && ( newline_added || total_size == 0 ) )
    unterminated_line = search_line_node( last_addr() );
  return total_size;
  }


/* read a named file/pipe into the buffer; return line count, or -1 if error */
int read_file( const char * const filename, const int addr )
  {
  FILE * fp;
  long size;
  int ret;

  if( *filename == '!' ) fp = popen( filename + 1, "r" );
#ifdef __OS2__
  else fp = fopen( strip_escapes( filename ), textmode ? "r": "rb" );
#else
  else fp = fopen( strip_escapes( filename ), "r" );
#endif
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open input file" );
    return -1;
    }
  size = read_stream( fp, addr );
  if( *filename == '!' ) ret = pclose( fp ); else ret = fclose( fp );
  if( size < 0 ) return -1;
  if( ret != 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close input file" );
    return -1;
    }
  if( !scripted() ) printf( "%lu\n", size );
  return current_addr() - addr;
  }


/* write a range of lines to a stream */
static long write_stream( FILE * const fp, int from, const int to )
  {
  line_t * lp = search_line_node( from );
  long size = 0;

  while( from && from <= to )
    {
    int len;
    char * p = get_sbuf_line( lp );
    if( !p ) return -1;
    len = lp->len;
    if( from != last_addr() || !isbinary() || !unterminated_last_line() )
      p[len++] = '\n';
    size += len;
    while( --len >= 0 )
      if( fputc( *p++, fp ) == EOF )
        {
        show_strerror( 0, errno );
        set_error_msg( "Cannot write file" );
        return -1;
        }
    ++from; lp = lp->q_forw;
    }
  return size;
  }


/* write a range of lines to a named file/pipe; return line count */
int write_file( const char * const filename, const char * const mode,
                const int from, const int to )
  {
  FILE * fp;
  long size;
#ifdef __OS2__
  char *binmode;
#endif
  int ret;

#ifdef __OS2__
  if (textmode)
     binmode = (char *) mode;
  else {
     binmode = malloc (strlen (mode) + 2);
     if (!binmode)
       binmode = (char *) mode;
  }
  strcpy (binmode, mode);
  strcat (binmode, "b");
#endif
  if( *filename == '!' ) fp = popen( filename + 1, "w" );
#ifdef __OS2__
  else fp = fopen( strip_escapes( filename ), binmode );
#else
  else fp = fopen( strip_escapes( filename ), mode );
#endif
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open output file" );
    return -1;
    }
  size = write_stream( fp, from, to );
  if( *filename == '!' ) ret = pclose( fp ); else ret = fclose( fp );
  if( size < 0 ) return -1;
  if( ret != 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close output file" );
    return -1;
    }
  if( !scripted() ) printf( "%lu\n", size );
  return ( from && from <= to ) ? to - from + 1 : 0;
  }
