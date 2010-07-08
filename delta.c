/*
  diff.c -- binary diff generator

  Copyright 2004,2005 Michael Schroeder

  rewritten from bsdiff.c,
      http://www.freebsd.org/cgi/cvsweb.cgi/src/usr.bin/bsdiff
  added library interface and hash method, enhanced suffix method.
*/
/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <bzlib.h>

#include "delta.h"

struct bzblock {
  unsigned char *data;
  unsigned int len;
  bz_stream strm;
};

static struct bzblock *blockopen()
{
  struct bzblock *bzf;
  int ret;
  bzf = malloc(sizeof(*bzf));
  if (!bzf)
    return 0;
  bzf->strm.bzalloc = 0;
  bzf->strm.bzfree = 0;
  bzf->strm.opaque = 0;
  bzf->len = 0;
  ret = BZ2_bzCompressInit(&bzf->strm, 9, 0, 30);
  if (ret != BZ_OK)
    {
      free(bzf);
      return 0;
    }
  bzf->data = 0;
  bzf->strm.avail_in = 0;
  bzf->strm.next_in  = 0;
  bzf->strm.avail_out = 0;
  bzf->strm.next_out  = 0;
  return bzf;
}

static int blockwrite(struct bzblock *bzf, void *buf, int len)
{
  int ret;

  if (len <= 0)
    return len < 0 ? -1 : 0;
  bzf->strm.avail_in = len;
  bzf->strm.next_in = buf;
  for (;;)
    {
      if (bzf->strm.avail_out < 4096)
	{
	  if (bzf->len + 8192 < bzf->len)
	    return -1;
	  if (bzf->data)
	    bzf->data = realloc(bzf->data, bzf->len + 8192);
	  else
	    bzf->data = malloc(bzf->len + 8192);
	  if (!bzf->data)
	    return -1;
	  bzf->strm.avail_out = 8192;
	}
      bzf->strm.next_out = (char *)bzf->data + bzf->len;
      ret = BZ2_bzCompress(&bzf->strm, BZ_RUN);
      if (ret != BZ_RUN_OK)
	return -1;
      bzf->len = (unsigned char *)bzf->strm.next_out - bzf->data;
      if (bzf->strm.avail_in == 0)
	return len;
    }
}

static int blockclose(struct bzblock *bzf, unsigned char **datap, unsigned int *lenp)
{
  int ret;
  bzf->strm.avail_in = 0;
  bzf->strm.next_in = 0;
  for (;;)
    {
      if (bzf->strm.avail_out < 4096)
	{
	  if (bzf->len + 8192 < bzf->len)
	    return -1;
	  if (bzf->data)
	    bzf->data = realloc(bzf->data, bzf->len + 8192);
	  else
	    bzf->data = malloc(bzf->len + 8192);
	  if (!bzf->data)
	    return -1;
	  bzf->strm.avail_out = 8192;
	}
      bzf->strm.next_out = (char *)bzf->data + bzf->len;
      ret = BZ2_bzCompress(&bzf->strm, BZ_FINISH);
      if (ret != BZ_FINISH_OK && ret != BZ_STREAM_END)
        return -1;
      bzf->len = (unsigned char *)bzf->strm.next_out - bzf->data;
      if (ret == BZ_STREAM_END)
        break;
    }
  BZ2_bzCompressEnd (&bzf->strm);
  *datap = bzf->data;
  *lenp = bzf->len;
  free(bzf);
  return 0;
}

static inline bsuint matchlen(unsigned char *old, bsuint oldlen, unsigned char *new, bsuint newlen)
{
  unsigned char *oldsave;
  bsuint max;
  oldsave = old;
  max = oldlen > newlen ? newlen : oldlen;
  while (max-- > 0)
    if (*old++ != *new++)
      return old - oldsave - 1;
  return old - oldsave;
}

#ifndef BSDIFF_NO_HASH

/******************************************************************/
/*                                                                */
/*         hash method                                            */
/*                                                                */
/******************************************************************/

#define HSIZESHIFT      4
#define HSIZE           (1 << HSIZESHIFT)

/* 256 random numbers generated by a quantum source */
static unsigned int noise[256] =
{
  0x9be502a4U, 0xba7180eaU, 0x324e474fU, 0x0aab8451U, 0x0ced3810U,
  0x2158a968U, 0x6bbd3771U, 0x75a02529U, 0x41f05c14U, 0xc2264b87U,
  0x1f67b359U, 0xcd2d031dU, 0x49dc0c04U, 0xa04ae45cU, 0x6ade28a7U,
  0x2d0254ffU, 0xdec60c7cU, 0xdef5c084U, 0x0f77ffc8U, 0x112021f6U,
  0x5f6d581eU, 0xe35ea3dfU, 0x3216bfb4U, 0xd5a3083dU, 0x7e63e9cdU,
  0xaa9208f6U, 0xda3f3978U, 0xfe0e2547U, 0x09dfb020U, 0xd97472c5U,
  0xbbce2edeU, 0x121aebd2U, 0x0e9fdbebU, 0x7b6f5d9cU, 0x84938e43U,
  0x30694f2dU, 0x86b7a7f8U, 0xefaf5876U, 0x263812e6U, 0xb6e48ddfU,
  0xce8ed980U, 0x4df591e1U, 0x75257b35U, 0x2f88dcffU, 0xa461fe44U,
  0xca613b4dU, 0xd9803f73U, 0xea056205U, 0xccca7a89U, 0x0f2dbb07U,
  0xc53e359eU, 0xe80d0137U, 0x2b2d2a5dU, 0xcfc1391aU, 0x2bb3b6c5U,
  0xb66aea3cU, 0x00ea419eU, 0xce5ada84U, 0xae1d6712U, 0x12f576baU,
  0x117fcbc4U, 0xa9d4c775U, 0x25b3d616U, 0xefda65a8U, 0xaff3ef5bU,
  0x00627e68U, 0x668d1e99U, 0x088d0eefU, 0xf8fac24dU, 0xe77457c7U,
  0x68d3beb4U, 0x921d2acbU, 0x9410eac9U, 0xd7f24399U, 0xcbdec497U,
  0x98c99ae1U, 0x65802b2cU, 0x81e1c3c4U, 0xa130bb09U, 0x17a87badU,
  0xa70367d6U, 0x148658d4U, 0x02f33377U, 0x8620d8b6U, 0xbdac25bdU,
  0xb0a6de51U, 0xd64c4571U, 0xa4185ba0U, 0xa342d70fU, 0x3f1dc4c1U,
  0x042dc3ceU, 0x0de89f43U, 0xa69b1867U, 0x3c064e11U, 0xad1e2c3eU,
  0x9660e8cdU, 0xd36b09caU, 0x4888f228U, 0x61a9ac3cU, 0xd9561118U,
  0x3532797eU, 0x71a35c22U, 0xecc1376cU, 0xab31e656U, 0x88bd0d35U,
  0x423b20ddU, 0x38e4651cU, 0x3c6397a4U, 0x4a7b12d9U, 0x08b1cf33U,
  0xd0604137U, 0xb035fdb8U, 0x4916da23U, 0xa9349493U, 0xd83daa9bU,
  0x145f7d95U, 0x868531d6U, 0xacb18f17U, 0x9cd33b6fU, 0x193e42b9U,
  0x26dfdc42U, 0x5069d8faU, 0x5bee24eeU, 0x5475d4c6U, 0x315b2c0cU,
  0xf764ef45U, 0x01b6f4ebU, 0x60ba3225U, 0x8a16777cU, 0x4c05cd28U,
  0x53e8c1d2U, 0xc8a76ce5U, 0x8045c1e6U, 0x61328752U, 0x2ebad322U,
  0x3444f3e2U, 0x91b8af11U, 0xb0cee675U, 0x55dbff5aU, 0xf7061ee0U,
  0x27d7d639U, 0xa4aef8c9U, 0x42ff0e4fU, 0x62755468U, 0x1c6ca3f3U,
  0xe4f522d1U, 0x2765fcb3U, 0xe20c8a95U, 0x3a69aea7U, 0x56ab2c4fU,
  0x8551e688U, 0xe0bc14c2U, 0x278676bfU, 0x893b6102U, 0xb4f0ab3bU,
  0xb55ddda9U, 0xa04c521fU, 0xc980088eU, 0x912aeac1U, 0x08519badU,
  0x991302d3U, 0x5b91a25bU, 0x696d9854U, 0x9ad8b4bfU, 0x41cb7e21U,
  0xa65d1e03U, 0x85791d29U, 0x89478aa7U, 0x4581e337U, 0x59bae0b1U,
  0xe0fc9df3U, 0x45d9002cU, 0x7837464fU, 0xda22de3aU, 0x1dc544bdU,
  0x601d8badU, 0x668b0abcU, 0x7a5ebfb1U, 0x3ac0b624U, 0x5ee16d7dU,
  0x9bfac387U, 0xbe8ef20cU, 0x8d2ae384U, 0x819dc7d5U, 0x7c4951e7U,
  0xe60da716U, 0x0c5b0073U, 0xb43b3d97U, 0xce9974edU, 0x0f691da9U,
  0x4b616d60U, 0x8fa9e819U, 0x3f390333U, 0x6f62fad6U, 0x5a32b67cU,
  0x3be6f1c3U, 0x05851103U, 0xff28828dU, 0xaa43a56aU, 0x075d7dd5U,
  0x248c4b7eU, 0x52fde3ebU, 0xf72e2edaU, 0x5da6f75fU, 0x2f5148d9U,
  0xcae2aeaeU, 0xfda6f3e5U, 0xff60d8ffU, 0x2adc02d2U, 0x1dbdbd4cU,
  0xd410ad7cU, 0x8c284aaeU, 0x392ef8e0U, 0x37d48b3aU, 0x6792fe9dU,
  0xad32ddfaU, 0x1545f24eU, 0x3a260f73U, 0xb724ca36U, 0xc510d751U,
  0x4f8df992U, 0x000b8b37U, 0x292e9b3dU, 0xa32f250fU, 0x8263d144U,
  0xfcae0516U, 0x1eae2183U, 0xd4af2027U, 0xc64afae3U, 0xe7b34fe4U,
  0xdf864aeaU, 0x80cc71c5U, 0x0e814df3U, 0x66cc5f41U, 0x853a497aU,
  0xa2886213U, 0x5e34a2eaU, 0x0f53ba47U, 0x718c484aU, 0xfa0f0b12U,
  0x33cc59ffU, 0x72b48e07U, 0x8b6f57bcU, 0x29cf886dU, 0x1950955bU,
  0xcd52910cU, 0x4cecef65U, 0x05c2cbfeU, 0x49df4f6aU, 0x1f4c3f34U,
  0xfadc1a09U, 0xf2d65a24U, 0x117f5594U, 0xde3a84e6U, 0x48db3024U,
  0xd10ca9b5U
};

/* buzhash by Robert C. Uzgalis */
/* General hash functions. Technical Report TR-92-01, The University
   of Hong Kong, 1993 */

static unsigned int buzhash(unsigned char *buf)
{
  unsigned int x = 0x83d31df4U;
  int i;
  for (i = HSIZE; i != 0; i--)
    x = (x << 1) ^ (x & (1 << 31) ? 1 : 0) ^ noise[*buf++];
  return x;
}

static unsigned int primes[] =
{
  65537, 98317, 147481, 221227, 331841, 497771, 746659, 1120001,
  1680013, 2520031, 3780053, 5670089, 8505137, 12757739, 19136609,
  28704913, 43057369, 64586087, 96879131, 145318741, 217978121,
  326967209, 490450837, 735676303, 1103514463, 1655271719,
  0xffffffff
};

struct hash_data {
  bsuint *hash;
  unsigned int prime;
};

static void *hash_create(unsigned char *buf, bsuint len)
{
  struct hash_data *hd;
  bsuint *hash;
  unsigned char *bp = buf;
  bsuint off;
  unsigned int s;
  unsigned int prime;
  unsigned int num;

  hd = malloc(sizeof(*hd));
  if (!hd)
    return 0;
#ifdef BSDIFF_64BIT
  /* this is a 16GB limit for HSIZESHIFT == 4 */
  if (len >= (bsuint)(0xffffffff / 4) << HSIZESHIFT)
    return 0;
#endif
  num = (len + HSIZE - 1) >> HSIZESHIFT;
  prime = num * 4;
  for (s = 0; s < sizeof(primes)/sizeof(*primes) - 1; s++)
    if (prime < primes[s])
      break;
  prime = primes[s];
  hash = calloc(prime, sizeof(*hash));
  if (!hash)
    {
      free(hd);
      return 0;
    }
  for (off = 0; len >= HSIZE; off += HSIZE, buf += HSIZE, len -= HSIZE)
    {
      s = buzhash(buf) % prime;
      if (hash[s])
        {
          if (hash[(s == prime - 1) ? 0 : s + 1])
            continue;
          if (!memcmp(buf, bp + hash[s], HSIZE))
            continue;
          s = (s == prime - 1) ? 0 : s + 1;
        }
      hash[s] = off + 1;
    }
  hd->hash = hash;
  hd->prime = prime;
  return hd;
}

static void hash_free(void *data)
{
  struct hash_data *hd = data;
  free(hd->hash);
  free(hd);
}

static bsuint hash_findnext(void *data, unsigned char *old, bsuint oldlen, unsigned char *new, bsuint newlen, bsuint lastoffset, bsuint scan, bsuint *posp, bsuint *lenp)
{
  struct hash_data *hd = data;
  bsuint scanstart, oldscore, oldscorenum, oldscorestart;
  bsuint pos, len;
  bsuint lscan, lpos, llen;
  bsuint i, ss, scsc;
  unsigned int ssx;
  unsigned int prime;
  bsuint *hash;

  hash = hd->hash;
  prime = hd->prime;
  scanstart = scan;
  oldscore = oldscorenum = oldscorestart = 0;
  ssx = scan <= newlen - HSIZE ? buzhash(new + scan) : 0;
  pos = 0;
  len = 0;
  lpos = lscan = llen = 0;
  for (;;)
    {
      if (scan >= newlen - HSIZE)
	{
	  if (llen >= 32)
	    goto gotit;
	  break;
	}
      ss = ssx % prime;
      pos = hash[ss];
      if (!pos)
	{
	  unsigned int oldc;
scannext:
	  if (llen >= 32 && scan - lscan >= HSIZE)
	    goto gotit;
	  ssx = (ssx << 1) ^ (ssx & (1 << 31) ? 1 : 0) ^ noise[new[scan + HSIZE]];
	  oldc = noise[new[scan]] ^ (0x83d31df4U ^ 0x07a63be9U);
#if HSIZE % 32 != 0
	  ssx ^= (oldc << (HSIZE % 32)) ^ (oldc >> (32 - (HSIZE % 32)));
#else
	  ssx ^= oldc;
#endif
	  scan++;
	  continue;
	}
      pos--;
      if (memcmp(old + pos, new + scan, HSIZE))
	{
	  pos = hash[ss == prime - 1 ? 0 : ss + 1];
	  if (!pos)
	    goto scannext;
	  pos--;
	  if (memcmp(old + pos, new + scan, HSIZE))
	    goto scannext;
	}
      len = matchlen(old + pos + HSIZE, oldlen - pos - HSIZE, new + scan + HSIZE, newlen - scan - HSIZE) + HSIZE;
      if (scan + HSIZE * 4 <= newlen)
	{
	  unsigned int ssx2;
	  bsuint len2, pos2;
	  ssx2 = buzhash(new + scan + HSIZE * 3) % prime;
	  pos2 = hash[ssx2];
	  if (pos2)
	    {
	      if (memcmp(new + scan + HSIZE *3, old + pos2 - 1, HSIZE))
		{
		  ssx2 = (ssx2 == prime) ? 0 : ssx2 + 1;
		  pos2 = hash[ssx2];
		}
	    }
	  if (pos2 > 1 + HSIZE*3)
	    {
	      pos2 = pos2 - 1 - HSIZE*3;
	      if (pos2 != pos)
		{
		  len2 = matchlen(old + pos2, oldlen - pos2, new + scan, newlen - scan);
		  if (len2 > len)
		    {
		      pos = pos2;
		      len = len2;
		    }
		}
	    }
	}
      if (len > llen)
	{
	  llen = len;
	  lpos = pos;
	  lscan = scan;
	}
      goto scannext;
gotit:
      scan = lscan;
      len = llen;
      pos = lpos;
      if (scan + lastoffset == pos)
	{
	  scan += len;
	  scanstart = scan;
	  if (scan + HSIZE < newlen)
	    ssx = buzhash(new + scan);
	  llen = 0;
	  continue;
	}
      for (i = scan - scanstart; i && pos && scan && old[pos - 1] == new[scan - 1]; i--)	
	{
	  len++;
	  pos--;
	  scan--;
	}
      if (oldscorestart + 1 != scan || oldscorenum == 0 || oldscorenum - 1 > len)
	{
	  oldscore = 0;
	  for (scsc = scan; scsc<scan+len; scsc++)
	    if ((scsc+lastoffset<oldlen) && (old[scsc+lastoffset] == new[scsc]))
	      oldscore++;
	  oldscorestart = scan;
	  oldscorenum = len;
	}
      else
	{
	  if (oldscorestart + lastoffset < oldlen && old[oldscorestart + lastoffset] == new[oldscorestart])
	    oldscore--;
	  oldscorestart++;
	  oldscorenum--;
	  for (scsc = oldscorestart + oldscorenum; oldscorenum < len; scsc++)
	    {
	      if ((scsc+lastoffset<oldlen) && (old[scsc+lastoffset] == new[scsc]))
		oldscore++;
		oldscorenum++;
	    }
	}
      if (len - oldscore >= 32)
	break;
      if (len > HSIZE * 3 + 32)
        scan += len - (HSIZE * 3 + 32);
      if (scan <= lscan)
	scan = lscan + 1;
      scanstart = scan;
      if (scan + HSIZE < newlen)
	ssx = buzhash(new + scan);
      llen = 0;
    }
  if (scan >= newlen - HSIZE)
    {
      scan = newlen;
      pos = 0;
      len = 0;
    }
  *posp = pos;
  *lenp = len;
  return scan;
}

#endif /* BSDIFF_NO_HASH */

#ifndef BSDIFF_NO_SUF

/******************************************************************/
/*                                                                */
/*         suffix list method                                     */
/*                                                                */
/******************************************************************/

struct suf_data {
  bsint *I;
  bsint F[257];
};

static void suf_split(bsint *I, bsint *V, bsint start, bsint len, bsint h)
{
  bsint i, j, k, x, tmp, jj, kk;

  if (len < 16)
    {
      for (k = start; k < start + len; k += j)
	{
	  j = 1;
          x = V[I[k] + h];
	  for (i = 1; k + i < start + len; i++)
	    {
	      if (V[I[k + i] + h] < x)
		{
		  x = V[I[k + i] + h];
		  j = 0;
		}
	      if(V[I[k + i] + h] == x)
		{
		  tmp = I[k+j];
                  I[k+j] = I[k+i];
                  I[k+i] = tmp;
		  j++;
		}
	    }
	  for(i = 0; i < j; i++)
	     V[I[k+i]] = k + j - 1;
	  if(j == 1)
	     I[k] = -1;
	}
      return;
    }

  x = V[I[start + len / 2] + h];
  jj = 0;
  kk = 0;
  for (i = start; i < start + len; i++)
    {
      if(V[I[i] + h] < x)
	 jj++;
      if(V[I[i] + h] == x)
	 kk++;
    };
  jj += start;
  kk += jj;

  i = start;
  j = 0;
  k = 0;
  while (i < jj)
    {
      if(V[I[i]+h]<x)
	{
	  i++;
	}
      else if(V[I[i]+h]==x)
	{
	  tmp = I[i];
          I[i] = I[jj + j];
          I[jj + j] = tmp;
	  j++;
	}
      else
	{
	  tmp = I[i];
          I[i] = I[kk + k];
          I[kk + k] = tmp;
	  k++;
	}
    }
  while (jj + j < kk)
    {
      if(V[I[jj+j]+h]==x)
	{
	  j++;
	}
      else
	{
	  tmp = I[jj + j];
	  I[jj + j] = I[kk + k];
	  I[kk + k] = tmp;
	  k++;
	}
    }

  if(jj > start)
    suf_split(I, V, start, jj-start, h);

  for (i=0; i < kk - jj; i++)
    V[I[jj + i]] = kk - 1;
  if (jj == kk - 1)
    I[jj] = -1;

  if (start + len > kk)
    suf_split(I, V, kk, start + len - kk, h);
}

static bsint suf_bucketsort(bsint *V, bsint *I, bsint n, bsint s)
{
  bsint c, d, g, i, j;
  bsint *B; 

  B = calloc(s, sizeof(bsint));
  if (!B)
    return 0;
  for (i = n - 1; i >= 0; i--)
    {
      c = V[i]; 
      V[i] = B[c]; 
      B[c] = i + 1;
    }
  for (j = s - 1, i = n; i; j--)
    {
      for (d = B[j], g = i; d; i--)
	{
	  c = d - 1;
	  d = V[c]; 
	  V[c] = g;
	  I[i] = !d && g == i ? -1 : c; 
	}
    }
  V[n] = 0;
  I[0] = -1;
  free(B);
  return 1;
}

static void *suf_create(unsigned char *buf, bsuint ulen)
{
  struct suf_data *sd;
  bsint *V, *I;
  bsint i, h, l, oldv, s, len;

  len = ulen;
  if (len < 0)
    return 0;
  sd = malloc(sizeof(*sd));
  if (!sd)
    return 0;
  V = malloc(sizeof(bsint) * (len + 3));
  if (!V)
    {
      free(sd);
      return 0;
    }
  I = malloc(sizeof(bsint) * (len + 3));
  if (!I)
    {
      free(V);
      free(sd);
      return 0;
    }
  memset(sd->F, 0, sizeof(sd->F));
  if (len >= 0x1000000)
    {
      s = 0x1000002;
      sd->F[buf[0]]++;
      sd->F[buf[1]]++;
      oldv = buf[0] << 8 | buf[1];
      for (i = 2; i < len; i++)
	{
	  sd->F[buf[i]]++;
	  oldv = (oldv & 0xffff) << 8 | buf[i];
	  V[i - 2] = oldv + 2;
        }
      oldv = (oldv & 0xffff) << 8;
      V[len - 2] = oldv + 2;
      oldv = (oldv & 0xffff) << 8;
      V[len - 1] = oldv + 2;
      len += 2;
      V[len - 2] = 1;
      V[len - 1] = 0;
      h = 3;
    }
  else
    {
      s = 0x10001;
      sd->F[buf[0]]++;
      oldv = buf[0];
      for (i = 1; i < len; i++)
	{
	  sd->F[buf[i]]++;
	  oldv = (oldv & 0xff) << 8 | buf[i];
	  V[i - 1] = oldv + 1;
	}
      oldv = (oldv & 0xff) << 8;
      V[len - 1] = oldv + 1;
      len += 1;
      V[len - 1] = 0;
      h = 2;
    }
  oldv = len;
  for (i = 256; i > 0; i--)
    {
      sd->F[i] = oldv;
      oldv -= sd->F[i - 1];
    }
  sd->F[0] = oldv;
  if (!suf_bucketsort(V, I, len, s))
    {
      free(I);
      free(V);
      free(sd);
      return 0;
    }
  for(; I[0] != -(len + 1); h += h)
    {
      l=0;
      for (i = 0; i < len + 1; )
	{
          if (I[i] < 0)
	    {
              l -= I[i];
              i -= I[i];
            }
	  else
	    {
              if(l)
		 I[i - l] = -l;
              l = V[I[i]] + 1 - i;
              suf_split(I, V, i, l, h);
              i += l;
              l = 0;
            }
        }
      if (l)
	 I[i - l] = -l;
    }
  for (i = 0; i < len + 1; i++)
    I[V[i]] = i;
  free(V);
  sd->I = I;
  return sd;
}

static void suf_free(void *data)
{
  struct suf_data *sd = data;
  free(sd->I);
  free(sd);
}

static bsuint suf_bsearch(bsint *I, unsigned char *old, bsuint oldlen, unsigned char *new, bsuint newlen, bsuint st, bsuint en, bsuint *posp)
{
  bsuint x, y;
  if (st > en)
    return 0;
  while (en - st >= 2)
    {
      x = st + (en - st) / 2;
      if (memcmp(old + I[x], new, oldlen - I[x] < newlen ? oldlen - I[x] : newlen) < 0)
	st = x;
      else
	en = x;
    }
  x = matchlen(old + I[st], oldlen - I[st], new, newlen);
  y = matchlen(old + I[en], oldlen - I[en], new, newlen);
  *posp = x > y ? I[st] : I[en];
  return x > y ? x : y;
}

static bsuint suf_findnext(void *data, unsigned char *old, bsuint oldlen, unsigned char *new, bsuint newlen, bsuint lastoffset, bsuint scan, bsuint *posp, bsuint *lenp)
{
  bsuint scsc, oldscore, len;

  struct suf_data *sd = data;
  len = 0;
  *posp = 0;
  scsc = scan;
  oldscore = 0;
  while (scan < newlen)
    {
      len = suf_bsearch(sd->I, old, oldlen, new + scan, newlen - scan, sd->F[new[scan]] + 1, sd->F[new[scan] + 1], posp);
      for (; scsc < scan + len; scsc++)
	if (scsc + lastoffset < oldlen && old[scsc + lastoffset] == new[scsc])
	  oldscore++;
      if (len && len == oldscore)
	{
	  scan += len;
	  scsc = scan;
	  oldscore = 0;
	  continue;
	}
      if (len > oldscore + 32)
	break;
      if (scan + lastoffset < oldlen && old[scan + lastoffset] == new[scan])
	oldscore--;
      scan++;
    }
  *lenp = len;
  return scan;
}

#endif /* BSDIFF_NO_SUF */

/******************************************************************/

struct deltamode {
  int mode;
  void *(*create)(unsigned char *buf, bsuint len);
  bsuint (*findnext)(void *data, unsigned char *old, bsuint oldlen, unsigned char *new, bsuint newlen, bsuint lastoffset, bsuint scan, bsuint *posp, bsuint *lenp);
  void (*free)(void *data);
};

struct deltamode deltamodes[] =
{
#ifndef BSDIFF_NO_SUF
  {DELTAMODE_SUF, suf_create, suf_findnext, suf_free}, 
#endif
#ifndef BSDIFF_NO_HASH
  {DELTAMODE_HASH, hash_create, hash_findnext, hash_free},
#endif
};

static void addoff(struct bzblock *bzi, bsint off)
{
  int i, sign = 0;
  unsigned char b[8];

  if (off < 0)
    {
      sign = 0x80;
      off = -off;
    }
  for (i = 0; i < 7; i++)
    {
      b[i] = off & 0xff;
      off >>= 8;
    }
  b[7] = sign | (off & 0xff);
  blockwrite(bzi, b, 8);
}

void mkdiff(int mode,
            unsigned char *old, bsuint oldlen,
            unsigned char *new, bsuint newlen,
            struct instr **instrp, int *instrlenp,
            unsigned char **instrblkp, unsigned int *instrblklenp,
            unsigned char **addblkp, unsigned int *addblklenp,
            unsigned char **extrablkp, unsigned int *extrablklenp)
{
  struct instr *instr = 0;
  int instrlen = 0;
  bsuint i, scan, pos, len;
  bsuint lastscan, lastpos, lastoffset;
  bsuint s, Sf, lenf, Sb, lenb;
  bsuint overlap, Ss, lens;
  struct bzblock *bza = 0;
  struct bzblock *bze = 0;
  struct bzblock *bzi = 0;
  void *data;
  struct deltamode *dm;
  int noaddblk = 0;

  if ((mode & DELTAMODE_NOADDBLK) != 0)
    {
      mode ^= DELTAMODE_NOADDBLK;
      noaddblk = 1;
    }
  dm = 0;
  for (i = 0; i < sizeof(deltamodes)/sizeof(*deltamodes); i++)
    {
      dm = deltamodes + i;
      if (deltamodes[i].mode == mode)
	break;
    }
  if (!dm)
    {
      fprintf(stderr, "mkdiff: no mode installed\n");
      exit(1);
    }
  if (addblkp)
    {
      *addblkp = 0;
      *addblklenp = 0;
    }
  if (extrablkp)
    {
      *extrablkp = 0;
      *extrablklenp = 0;
    }
  if (instrblkp)
    {
      *instrblkp = 0;
      *instrblklenp = 0;
    }
  if (!noaddblk && addblkp && (bza = blockopen()) == 0)
    {
      fprintf(stderr, "mkdiff: could not create compression stream\n");
      exit(1);
    }
  if (extrablkp && (bze = blockopen()) == 0)
    {
      fprintf(stderr, "mkdiff: could not create compression stream\n");
      exit(1);
    }
  if (instrblkp && (bzi = blockopen()) == 0)
    {
      fprintf(stderr, "mkdiff: could not create compression stream\n");
      exit(1);
    }
  data = dm->create(old, oldlen);
  if (!data)
    {
      fprintf(stderr, "mkdiff: could not create data\n");
      exit(1);
    }

  scan = 0; len = 0; lenf = 0;
  lastscan = 0; lastpos = 0;

  while (lastscan < newlen)
    {
      /* search for data matching something in new[scan...]
       * input:
       *   old/oldlen, new/newlen: search data
       *   lastoffset: lastpos - lastscan  (because we want to find a different match)
       * returns:
       *   scan: start of match in new[]
       *   pos:  start of match in old[]
       *   len:  length of match
       * (if no match is found, scan = newlen, len = 0)
       */
      lastoffset = noaddblk ? oldlen : lastpos - lastscan;
      scan = dm->findnext(data, old, oldlen, new, newlen, lastoffset, scan, &pos, &len);

      /* extand old match forward */
      if (!noaddblk)
	{
	  s = Sf = lenf = 0;
	  for (i = 0; lastscan + i < scan && lastpos + i < oldlen; )
	    {
	      if (old[lastpos+i] == new[lastscan+i])
		{
		  s++;
		  i++;
		  if (s >= Sf + i - s)
		    {
		      Sf = 2 * s - i;
		      lenf = i;
		    }
		}
	      else
	        i++;
	    }
	}
      else
	{
	  for (i = 0; lastscan + i < scan && lastpos + i < oldlen; i++)
	    if (old[lastpos+i] != new[lastscan+i])
	      break;
	  lenf = i;
	}

      lenb = 0;
      /* extand new match backward, scan == newlen means we're going to finish */
      if (!noaddblk && scan < newlen)
	{
	  s = Sb = 0;
	  for(i = 1; scan >= lastscan + i && pos >= i; i++)
	    {
	      if (old[pos-i] == new[scan-i])
		{
		  s++;
		  if(s >= Sb + i - s)
		    {
		      Sb = 2 * s - i;
		      lenb = i;
		    }
		}
	    }
	}

      /* if there is an overlap find good place to split */
      if (lastscan + lenf > scan - lenb)
	{
	  overlap = (lastscan + lenf) - (scan - lenb);
	  s = Sb = Ss = lens = 0;
	  for (i = 0; i < overlap; i++)
	    {
	      if(new[lastscan + lenf - overlap + i] == old[lastpos + lenf - overlap + i])
		s++;
	      if(new[scan - lenb + i] == old[pos - lenb + i])
		Sb++;
	      if (s > Sb && s - Sb > Ss)
		{
		  Ss = s - Sb;
		  lens = i + 1;
		}
	    }
	  lenf -= overlap - lens;
	  lenb -= lens;
	}

      /*
       *         lastscan                    scan
       *            |--- lenf ---|    |- lenb -|-- len --|
       *            |            |    |        |         |
       * new: ------+=======-----+----+--------+=========+--
       *           /                           \
       *          /                             |
       * old: ---+=======-----------------------+=========---
       *         |                              |
       *      lastpos                          pos
       */

      if (instrp)
	{
	  if ((instrlen & 31) == 0)
	    {
	      if (instr)
	        instr = realloc(instr, sizeof(*instr) * (instrlen + 32));
	      else
	        instr = malloc(sizeof(*instr) * (instrlen + 32));
	      if (!instr)
		{
		  fprintf(stderr, "out of memory\n");
		  exit(1);
		}
	    }
	  instr[instrlen].copyout = lenf;
	  instr[instrlen].copyin = (scan - lenb) - (lastscan + lenf);
	  instr[instrlen].copyinoff = lastscan + lenf;
	  instr[instrlen].copyoutoff = lastpos;
	  instrlen++;
	}
      if (bzi)
	{
	  addoff(bzi, lenf);
	  addoff(bzi, (scan - lenb) - (lastscan + lenf));
	  addoff(bzi, (pos - lenb) - (lastpos + lenf));
	}
      if (bze)
	{
	  i = lastscan + lenf;
	  s = (scan - lenb) - i;
	  while (s > 0)
	    {
	      int len2 = s > 0x40000000 ? 0x40000000 : s;
	      blockwrite(bze, new + i, len2);
	      i += len2;
	      s -= len2;
	    }
	}
      if (bza)
	{
	  while (lenf > 0)
	    {
	      unsigned char addblk[4096];
	      int len2;
	      len2 = lenf > 4096 ? 4096 : lenf;
	      for (i = 0; i < len2; i++)
		addblk[i] = new[lastscan + i] - old[lastpos + i];
	      if (blockwrite(bza, addblk, len2) != len2)
		{
		  fprintf(stderr, "could not append to data block\n");
		  exit(1);
		}
	      lastscan += len2;
	      lastpos += len2;
	      lenf -= len2;
	    }
	}

      /* advance */
      lastscan = scan - lenb;
      lastpos = pos - lenb;
      scan += len;
    }
  if (bza && blockclose(bza, addblkp, addblklenp))
    {
      fprintf(stderr, "could not close data block\n");
      exit(1);
    }
  if (bze && blockclose(bze, extrablkp, extrablklenp))
    {
      fprintf(stderr, "could not close extra block\n");
      exit(1);
    }
  if (bzi && blockclose(bzi, instrblkp, instrblklenp))
    {
      fprintf(stderr, "could not close instr block\n");
      exit(1);
    }
  if (instrp)
    {
      *instrp = instr;
      *instrlenp = instrlen;
    }
  dm->free(data);
}

struct stepdata {
  struct deltamode *dm;
  void *data;
  int noaddblk;
};

void *
mkdiff_step_setup(int mode)
{
  int noaddblk = 0;
  struct stepdata *sd;
  struct deltamode *dm;
  int i;

  if ((mode & DELTAMODE_NOADDBLK) != 0)
    {
      mode ^= DELTAMODE_NOADDBLK;
      noaddblk = 1;
    }
  dm = 0;
  for (i = 0; i < sizeof(deltamodes)/sizeof(*deltamodes); i++)
    {
      dm = deltamodes + i;
      if (deltamodes[i].mode == mode)
	break;
    }
  if (!dm)
    {
      fprintf(stderr, "mkdiff: no mode installed\n");
      exit(1);
    }
  sd = calloc(1, sizeof(*sd));
  if (!sd)
    return 0;
  sd->dm = dm;
  sd->data = 0;
  sd->noaddblk = noaddblk;
  return sd;
}

void
mkdiff_step(void *sdata,
            unsigned char *old, bsuint oldlen,
            unsigned char *new, bsuint newlen,
            struct instr *instr,
	    bsuint *scanp, bsuint *lastposp, bsuint *lastscanp)
  
{
  struct stepdata *sd = sdata;
  struct deltamode *dm = sd->dm;
  bsuint scan, lastpos, lastscan;
  bsuint pos, len, lastoffset;
  bsuint s, Sf, lenf, Sb, lenb;
  bsuint overlap, Ss, lens;
  bsuint i;

  if (!sd->data)
    {
      sd->data = dm->create(old, oldlen);
      if (!sd->data)
	{
	  fprintf(stderr, "mkdiff: could not create data\n");
	  exit(1);
	}
    }
  scan = *scanp;
  lastscan = *lastscanp;
  lastpos = *lastposp;

  lastoffset = sd->noaddblk ? oldlen : lastpos - lastscan;
  scan = dm->findnext(sd->data, old, oldlen, new, newlen, lastoffset, scan, &pos, &len);
  if (!sd->noaddblk)
    {
      s = Sf = lenf = 0;
      for (i = 0; lastscan + i < scan && lastpos + i < oldlen; )
	{
	  if (old[lastpos+i] == new[lastscan+i])
	    {
	      s++;
	      i++;
	      if (s >= Sf + i - s)
		{
		  Sf = 2 * s - i;
		  lenf = i;
		}
	    }
	  else
	    i++;
	}
    }
  else
    {
      for (i = 0; lastscan + i < scan && lastpos + i < oldlen; i++)
	if (old[lastpos+i] != new[lastscan+i])
	  break;
      lenf = i;
    }

  lenb = 0;
  /* extand new match backward, scan == newlen means we're going to finish */
  if (!sd->noaddblk && scan < newlen)
    {
      s = Sb = 0;
      for(i = 1; scan >= lastscan + i && pos >= i; i++)
	{
	  if (old[pos-i] == new[scan-i])
	    {
	      s++;
	      if(s >= Sb + i - s)
		{
		  Sb = 2 * s - i;
		  lenb = i;
		}
	    }
	}
    }

  /* if there is an overlap find good place to split */
  if (lastscan + lenf > scan - lenb)
    {
      overlap = (lastscan + lenf) - (scan - lenb);
      s = Sb = Ss = lens = 0;
      for (i = 0; i < overlap; i++)
	{
	  if(new[lastscan + lenf - overlap + i] == old[lastpos + lenf - overlap + i])
	    s++;
	  if(new[scan - lenb + i] == old[pos - lenb + i])
	    Sb++;
	  if (s > Sb && s - Sb > Ss)
	    {
	      Ss = s - Sb;
	      lens = i + 1;
	    }
	}
      lenf -= overlap - lens;
      lenb -= lens;
    }

  /* printf("MATCH len %d @ %d:%d-%d-%d:%d -- %d\n", len, lastscan, lenf, (scan - lenb) - (lastscan + lenf), lenb, scan, pos); */

  instr->copyout = lenf;
  instr->copyin = (scan - lenb) - (lastscan + lenf);
  instr->copyinoff = lastscan + lenf;
  instr->copyoutoff = lastpos;
  *scanp = scan + len;
  *lastscanp = scan - lenb;
  if (scan != newlen)
    *lastposp = pos - lenb;
  else
    *lastposp = lastpos + lenf;
}

void
mkdiff_step_freedata(void *sdata)
{
  struct stepdata *sd = sdata;
  struct deltamode *dm = sd->dm;
  if (sd->data)
    dm->free(sd->data);
  sd->data = 0;
}

void
mkdiff_step_free(void *sdata)
{
  struct stepdata *sd = sdata;
  struct deltamode *dm = sd->dm;
  if (sd->data)
    dm->free(sd->data);
  free(sd);
}
