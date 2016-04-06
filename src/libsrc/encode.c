/*-------------------------------------------------------------------------
  *
  * encode.c
  *    Various data encoding/decoding things.
  *
  * Copyright (c) 2001-2007, PostgreSQL Global Development Group
  *
  *
  * IDENTIFICATION
  *    $PostgreSQL: pgsql/src/backend/utils/adt/encode.c,v 1.19 2007/02/27 23:48:08 tgl Exp $
  *
  *-------------------------------------------------------------------------
  */
 #include <stdio.h>
 #include <ctype.h>
 #include "cMsg.h"
 
 /*
  * BASE64
  */
 
 static const char _base64[] =
 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 
 static const int8_t b64lookup[128] = {
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
     -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
     -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
 };
 
 
/*-------------------------------------------------------------------*/


 /**
  * This routine encodes the input binary data buffer into a string
  * of printable ASCII characters using the base64 encoding format.
  * It places a new line after each 76 characters.  User can 
  * calculate the size of the needed output buffer by calling routine
  * {@link cMsg_b64_encode_len}.
  * Carl Timmer modified this so and ending '\n' is always there.
  *
  * @param src pointer to input binary buffer
  * @param len number of bytes in src array to encode
  * @param dst pointer to output character array. Must be large enough
  *            to hold the encoding. Calculate necessary size with
  *            routine {@link cMsg_b64_encode_len}
  * @param lineBreaks Whether to insert line breaks every 76 characters
  *                   and at end in the output.
  *
  * @return number of characters in resulting character array
  */
 unsigned int
 cMsg_b64_encode(const char *src, unsigned int len, char *dst, int lineBreaks)
 {
     char       *p, *oldEnd,
                *lend = dst + 76;
     const char *s,
                *end = src + len;
     int         pos = 2;
     uint32_t    buf = 0;
 
     s = src;
     p = dst;
     oldEnd = p;
 
     while (s < end)
     {
         /* For pos = 2,1,0 we get right shifts (in successive 3 bytes) of 16,8,0 */
         buf |= (unsigned char) *s << (pos << 3);
         pos--;
         s++;
 
         /* write it out */
         if (pos < 0)
         {   /* The 3 bytes are left shifted 4 times and the bottom 6 bits masked in */
             *p++ = _base64[(buf >> 18) & 0x3f];
             *p++ = _base64[(buf >> 12) & 0x3f];
             *p++ = _base64[(buf >> 6) & 0x3f];
             *p++ = _base64[buf & 0x3f];
 
             pos = 2;
             buf = 0;
         }
         if (lineBreaks && p >= lend)
         {
             *p++ = '\n';
             oldEnd = p;
             lend = p + 76;
         }
     }
     /* pos == 2 means # of chars exactly matches # bytes */
     if (pos != 2)
     {
         *p++ = _base64[(buf >> 18) & 0x3f];
         *p++ = _base64[(buf >> 12) & 0x3f];
         *p++ = (char) ((pos == 0) ? _base64[(buf >> 6) & 0x3f] : '=');
         *p++ = '=';
     }
     if (lineBreaks && p > oldEnd)
     {
         *p++ = '\n';
     }

     return (unsigned int) (p - dst);
 }

 
/*-------------------------------------------------------------------*/


 /**
  * This routine takes an input buffer of base64 format, printable ASCII
  * characters and decodes it to the output buffer in binary. User can 
  * calculate the size of the needed output buffer by calling routine
  * {@link cMsg_b64_decode_len}.
  *
  * @param src pointer to buffer of base64 characters
  * @param len number of characters in src array to decode
  * @param dst pointer to output binary buffer. Must be large enough
  *            to hold the decoding. Calculate necessary size with
  *            routine {@link cMsg_b64_decode_len}
  *
  * @return  if successful, # of bytes in resulting binary array,
  *          -1 if unexpected "=",
  *          -2 if invalid symbol, or
  *          -3 if invalid end sequence
  */
 int
 cMsg_b64_decode(const char *src, unsigned int len, char *dst)
 {
     const char *srcend = src + len,
                *s = src;
     char       *p = dst;
     char        c;
     int         b = 0;
     uint32_t    buf = 0;
     int         pos = 0,
                 end = 0,
                 debug = 1;
 
     while (s < srcend)
     {
         c = *s++;
 
         if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
             continue;
 
         if (c == '=')
         {
             /* end sequence */
             if (!end)
             {
                 if (pos == 2)
                     end = 1;
                 else if (pos == 3)
                     end = 2;
                 else
                 {
                     if (debug) printf("cMsg_b64_decode: unexpected \"=\"\n");
                     return -1;
                 }
             }
             b = 0;
         }
         else
         {
             b = -1;
             if (c > 0 && c < 127)
                 b = b64lookup[(unsigned char) c];
             if (b < 0)
             {
                 if (debug) {
                   if (isprint(c))
                     printf("cMsg_b64_decode: invalid symbol (%c)\n", c);
                   else
                     printf("cMsg_b64_decode: invalid symbol\n");
                 }
                 return -2;
             }
         }
         /* add it to buffer */
         buf = (buf << 6) + b;
         pos++;
         if (pos == 4)
         {
             *p++ = (char) ((buf >> 16) & 255);
             if (end == 0 || end > 1)
                 *p++ = (char) ((buf >> 8) & 255);
             if (end == 0 || end > 2)
                 *p++ = (char) (buf & 255);
             buf = 0;
             pos = 0;
         }
     }
 
     if (pos != 0)
     {
        if (debug) printf("cMsg_b64_decode: invalid end sequence\n");
        return -3;
     }
 
     return (int) (p - dst);
 }
 
 
/*-------------------------------------------------------------------*/


 /**
  * This routine calculates the number of bytes in a decoded
  * binary representation of the given base64 string.
  * This is an exact calculation, not an estimate.
  *
  * @param src pointer to buffer containing base64 string
  * @param len number of characters in string
  *
  * @return number bytes needed to store decoded base64 string
  */
 unsigned int
 cMsg_b64_decode_len(const char *src, unsigned int len)
 {
     const char *srcend = src + len, *s = src;
     char         c;
     int          pos = 0, end = 0;
     unsigned int count = 0;
 
     while (s < srcend)
     {
         c = *s++;
 
         if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
             continue;
 
         if (c == '=') {
             if (!end) {
                 if (pos == 2)
                     end = 1;
                 else if (pos == 3)
                     end = 2;
             }
         }
         pos++;
         if (pos == 4) {
             count++;
             if (end == 0 || end > 1)
                 count++;
             if (end == 0 || end > 2)
                 count++;
             pos = 0;
         }
     }
  
     return count;
 }
 
 
/*-------------------------------------------------------------------*/


 /**
  * This routine calculates the number of characters in an encoded
  * string representation of the given binary buffer and byte length.
  * This is an exact calculation, not an estimate.
  *
  * @param src pointer to buffer containing binary data
  * @param len length of binary buffer in bytes
  * @param lineBreaks Whether to insert line breaks every 76 characters
  *                   and at end in the output.
  *
  * @return number of characters needed to encode given binary array
  */
 unsigned int
 cMsg_b64_encode_len(const char *src, unsigned int len, int lineBreaks)
 {
     const char   *s = src,
                  *end = src + len;
     unsigned int count = 0, lend = 76, oldEnd = 0;
              int pos = 2;
  
     while (s < end) {
         pos--;
         s++;
 
         /* write 4 chars (in 3 bytes) here */
         if (pos < 0) {
             count += 4; 
             pos = 2;
         }
         /* add newline here */
         if (lineBreaks && count >= lend) {
             count++;
             oldEnd = count;
             lend = count + 76;
         }
     }
     if (pos != 2) {
         count += 4; 
     }
     if (lineBreaks && count > oldEnd) {
         count++;
     }
 
     return count;
 }

 
 /*-------------------------------------------------------------------*/

 
 /**
  * This routine estimates the number of characters in an encoded
  * string representation of the given binary buffer and byte length.
  * This may be an overestimate but is faster than an exact calculation.
  *
  * @param src pointer to buffer containing binary data
  * @param srclen length of binary buffer in bytes
  *
  * @return number of characters needed to encode given binary array
  */
 unsigned int
 cMsg_b64_encode_len_est(const char *src, unsigned int srclen)
 {
     /* 3 bytes will be converted to 4, linefeed after 76 chars & 1 at end */
     return (srclen + 2) * 4 / 3 + srclen / (76 * 3 / 4) + 1;
 }
 
 
/*-------------------------------------------------------------------*/


 /**
  * This routine estimates the number of bytes in a decoded
  * binary representation of the given base64 string.
  * This may be an overestimate but is faster than an exact calculation.
  *
  * @param src pointer to buffer containing base64 string
  * @param srclen number of characters in string
  *
  * @return number bytes needed to store decoded base64 string
  */
 unsigned int
 cMsg_b64_decode_len_est(const char *src, unsigned int srclen)
 {
     return (srclen * 3) >> 2;
 }
 
