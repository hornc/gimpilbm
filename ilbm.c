#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <netinet/in.h>
#include <gtk/gtk.h>
#include <libgimp/gimp.h>

#include "plugin.h"
#include "gui.h"
#include "iff.h"
#include "byterun1.h"
#include "hamehb.h"
#include "ilbm.h"

/**** BMHD BitMap HeaDer ****/

static void
valBMHD(ILBMbmhd * bmhd)
{
  bmhd->w = ntohs(bmhd->w);
  bmhd->h = ntohs(bmhd->h);
  bmhd->x = ntohs(bmhd->x);
  bmhd->y = ntohs(bmhd->y);
  bmhd->transparentColor = ntohs(bmhd->transparentColor);
  bmhd->pageWidth = ntohs(bmhd->pageWidth);
  bmhd->pageHeight = ntohs(bmhd->pageHeight);
}

static void
genBMHD(ILBMbmhd * bmhd,
        guint16 width, guint16 height,
        guint8 depth)
{
  memset(bmhd, 0, sizeof(ILBMbmhd));
  bmhd->w = bmhd->pageWidth = htons(width);
  bmhd->h = bmhd->pageHeight = htons(height);
  bmhd->nPlanes = depth;
  bmhd->compression = cmpNone;
  bmhd->xAspect = bmhd->yAspect = 1;
}

static void
dumpBMHD(const ILBMbmhd * bmhd)
{
  printf("%ubit image %ux%u, max. %lu colors, %s\n",
         bmhd->nPlanes, bmhd->w, bmhd->h,
         1L << bmhd->nPlanes,
         (bmhd->nPlanes >= 12) ? "RGB" : "indexed"
    );
  /* RGB check okay? */
  printf("x/y = %d/%d, Masking:%u, Compr.:%u\n",
         bmhd->x, bmhd->y,
         bmhd->masking, bmhd->compression
    );
  printf("Transparent color: %u, Aspect: %u/%u\n",
         bmhd->transparentColor,
         bmhd->xAspect, bmhd->yAspect
    );
  printf("Page size: %dx%d\n",
         bmhd->pageWidth, bmhd->pageHeight
    );
}

/**** CAMG  Commodore AMiGa ****/

static void
valCAMG(ILBMcamg * camg)
{
  camg->viewModes = ntohl(camg->viewModes) & CAMGMASK;
}

static void
dumpCAMG(const ILBMcamg * camg)
{
  printf("ViewModes: 0x%08lx  ", (unsigned long) camg->viewModes);
  if (camg->viewModes & hiRes)
    fputs("HIRES ", stdout);
  if (camg->viewModes & ham)
    fputs("HAM ", stdout);
  if (camg->viewModes & extraHalfbrite)
    fputs("EHB ", stdout);
  if (camg->viewModes & lace)
    fputs("LACE ", stdout);
  fputs("\n", stdout);
}

/**** DPI  Dots Per Inch ****/

static void
valDPI(ILBMdpi * dpi)
{
  dpi->dpiX = ntohs(dpi->dpiX);
  dpi->dpiY = ntohs(dpi->dpiY);
}

static void
dumpDPI(const ILBMdpi * dpi)
{
  printf("Scan resolution: %hux%hu dpi\n",
         dpi->dpiX, dpi->dpiY);
}

/**** GRAB  Image hotspot ****/

static void
valGRAB(ILBMgrab * grab)
{
  grab->grabX = ntohs(grab->grabX);
  grab->grabY = ntohs(grab->grabY);
}

static void
dumpGRAB(const ILBMgrab * grab)
{
  printf("Grab hotspot: (%hd/%hd)\n",
         grab->grabX, grab->grabY);
}

/**** DEST  DestMerge masks ****/

static void
valDEST(ILBMdest * dest)
{
  dest->planePick = ntohs(dest->planePick);
  dest->planeOnOff = ntohs(dest->planeOnOff);
  dest->planeMask = ntohs(dest->planeMask);
}

static void
dumpDEST(ILBMdest * dest)
{
  printf("DEST depth: %hd\n"
         "     planePick: %04hX planeOnOff: %04hX\n"
         "     planeMask: %04hX\n",
         (short)dest->depth,
         dest->planePick, dest->planeOnOff,
         dest->planeMask);
}

/**** SPRT  Sprite ****/

static void
valSPRT(ILBMsprt * sprt)
{
  sprt->preced = ntohs(sprt->preced);
}

static void
dumpSPRT(ILBMsprt * sprt)
{
  printf("Sprite Precedence: %hu\n", sprt->preced);
}

/**** Grayscale ****/

static void
transGray(grayval * dest, gint width, const palidx * transGrayTab)
{
  g_assert(dest != NULL);
  g_assert(transGrayTab != NULL);
  while (width--) {
    *dest = transGrayTab[*dest];
    ++dest;
  }
}

static guint8 *
allocGrayscale(void)
{
  guint8 *gbuf = g_new(guint8, maxGrayshades * byteppRGB);
  if (!gbuf) {
    fputs("Out of memory.\n", stderr);
  } else {
    guint8 *gp;
    gint i;
    if (VERBOSE)
      fputs("Composing grayscale colormap...\n", stdout);
    for (gp = gbuf, i = 0; i < maxGrayshades; ++i) {
      *gp++ = i;
      *gp++ = i;
      *gp++ = i;
    }
  }
  return (gbuf);
}

/*  Returns TRUE if the given colormap consists of only
 *  gray entries
 */

static gboolean
isGrayscale(const guint8 * cmap, gint ncols)
{
  g_assert(cmap != NULL);
  g_assert(ncols >= 0);
  while (ncols--) {
    if (!((*cmap == cmap[1]) && (cmap[1] == cmap[2])))
      return (FALSE);
    cmap += byteppRGB;
  }
  return (TRUE);
}

/* Allocates a 256 byte array of the values 0..255 */

static guint8 *
allocGrayKeep(void)
{
  guint8 *gt = g_new(guint8, maxGrayshades);
  if (!gt) {
    fputs("Out of memory.\n", stderr);
  } else {
    gint i;
    for (i = maxGrayshades - 1; i >= 0; --i)
      gt[i] = i;
  }
  return (gt);
}

/*  Returns a ncols wide array
 *  "cmap" has to be grayscale
 */

static guint8 *
allocGrayTrans(const guint8 * cmap, gint ncols)
{
  guint8 *gt = g_new(guint8, maxGrayshades);
  g_assert(cmap != NULL);
  g_assert(ncols >= 0);
  if (!gt) {
    fputs("Out of memory.\n", stderr);
  } else {
    gint idx = 0;
    while (ncols--) {
      gt[idx] = *cmap;
      ++idx;
      cmap += byteppRGB;
    }
  }
  return (gt);
}

/**** RGBN8 ****/

static gboolean
unpackRGBN8(FILE * fil, grayval * rgbbuf, gint32 pixelNeeded,
            IffID ftype, gboolean alpha)
{
  gboolean success = 1;
  grayval ca = 0;               /* Make gcc happy */
  while (pixelNeeded > 0) {
    guint32 repeat;
    grayval cr, cg, cb;
    if (ID_RGBN == ftype) {
      guint16 cword;
      if (!(success = readUword(fil, &cword))) {
        break;
      } else {
        cr = ham4bitToGray8(cword >> 12);
        cg = ham4bitToGray8((cword >> 8) & 0xF);
        cb = ham4bitToGray8((cword >> 4) & 0xF);
        repeat = cword & 7;
        if (alpha) {
          ca = ((cword >> 3) & 1) ? transparent : opaque;
        }
      }
    } else {
      guint32 clong;
      if (!(success = readUlong(fil, &clong))) {
        break;
      } else {
        cr = clong >> 24;
        cg = (clong >> 16) & 0xFF;
        cb = (clong >> 8) & 0xFF;
        repeat = clong & 0x7F;
        if (alpha) {
          ca = ((clong >> 7) & 1) ? transparent : opaque;
        }
      }
    }
    if (!repeat) {
      guint8 cbyte;
      if (!(success = readUchar(fil, &cbyte))) {
        break;
      } else {
        if (cbyte) {
          repeat = cbyte;
        } else {
          guint16 cword;
          if (!(success = readUword(fil, &cword))) {
            break;
          } else {
            if (!cword) {
              repeat = 65536;
            } else {
              repeat = cword;
            }
          }
        }
      }
    }
    while (pixelNeeded && repeat--) {
      *rgbbuf++ = cr;
      *rgbbuf++ = cg;
      *rgbbuf++ = cb;
      if (alpha) {
        *rgbbuf++ = ca;
      }
      --pixelNeeded;
    }
  }
  return (success);
}


static void
unpackBits(const guint8 * bitlinebuf,
           guint8 * destline, gint bitnr, gint width)
{
  guint8 setmask = 1 << bitnr;
  g_assert(bitlinebuf != NULL);
  g_assert(destline != NULL);
  while (width > 0) {
    guint8 checkmask;
    for (checkmask = 1 << 7; width && checkmask; checkmask >>= 1) {
      if (*bitlinebuf & checkmask) {
        *destline |= setmask;
      }
      ++destline;
      --width;
    }
    ++bitlinebuf;
  }
}

static void
setBits(guint8 * destline, gint bitnr, gint width)
{
  guint8 setmask = 1 << bitnr;
  width <<= 3;	/* is now number of pixels */
  while (width > 0) {
    *destline |= setmask;
    ++destline;
    --width;
  }
}

static void
clrBits(guint8 * destline, gint bitnr, gint width)
{
  guint8 clrmask = ~(1 << bitnr);
  width <<= 3;	/* is now number of pixels */
  while (width > 0) {
    *destline &= clrmask;
    ++destline;
    --width;
  }
}

static gint
numBitsSet (guint32 val)
{
  gint bitsSet = 0;
  while ((val)) {
    if (val & 1) {
      ++bitsSet;
    }
    val >>= 1;
  }
  return (bitsSet);
}

static void
writeRGB(const grayval * destline, grayval * dest, gint numPixels, gint bytepp)
{
  g_assert(destline != NULL);
  g_assert(dest != NULL);
  g_assert(bytepp > 0);
  while (numPixels--) {
    *dest = *destline++;
    dest += bytepp;             /*4 */
  }
}

static void
bitExpandStep(guint8 * dest, const guint8 * src, gint width, gint step)
{
  /* Expands alpha masks to 0x00/0xFF style lines */
  guint8 bit = 1 << 7;
  g_assert(dest != NULL);
  g_assert(src != NULL);
  g_assert(step > 0);
  while (width--) {
    *dest = (*src & bit) ? opaque : transparent;
    dest += step;
    bit >>= 1;
    if (!bit) {
      bit = 1 << 7;
      ++src;
    }
  }
}

static gboolean
readPlaneRow(FILE * fil, guint8 * bitDest, gint bytesInPlaneRow, gint16 compression)
{
  gboolean success = 0;
  switch (compression) {
    case cmpByteRun1:
      success = unpackRow(fil, (gint8 *) bitDest, bytesInPlaneRow);
      break;
    case cmpRGBN:
    case cmpRGB8:
      fputs("(RGBN/RGB8 compression mode 4/x found)\n", stderr);
    default:                   /* fall-thru */
      fputs("Unknown compression mode---reading raw.\n", stderr);
    case cmpNone:
      success = iffReadData(fil, bitDest, bytesInPlaneRow);
      if (!success) {
        if (feof(fil))
          fputs("Unexpected EOF\n", stderr);
        else
          perror("readPlaneRow");
      }
      break;
  }
  return (success);
}

static guint8 *
allocPackedBuf(gint bytesInPlaneRow, gint16 compression)
{
  guint8 *packedBuf = NULL;     /* NULL is a valid value here */
  if (cmpByteRun1 == compression) {
    packedBuf = g_new(guint8, bytesInPlaneRow + 8);  /* FIXME: 8 enough? */
    if (!packedBuf) {
      fputs("Out of memory.\n", stderr);
    }
  }
  return (packedBuf);
}

static void
freePackedBuf(guint8 * packedBuf)
{
  if (NULL != packedBuf) {
    g_free(packedBuf);
  }
}

static gboolean
writePlaneRow(FILE * fil, const guint8 * bitSrc,
              gint bytesInPlaneRow, gint16 compression,
              guint8 * packedBuf)
{
  gboolean success;
  switch (compression) {
    case cmpByteRun1:
      {
        gint32 written;
        written = packRow(packedBuf, bitSrc, bytesInPlaneRow);
        if (written > 0) {
          success = iffWriteData(fil, packedBuf, written);
        } else
          success = 0;
      }
      break;
    default:                   /* Error message? */
    case cmpNone:
      success = iffWriteData(fil, bitSrc, bytesInPlaneRow);
      if (!success)
        perror("writePlaneRow");
      break;
  }
  if (!success)
    fprintf(stderr, "writePlaneRow: Error.\n");
  return (success);
}

static void
checkIdxRanges (palidx* row, gint width, gint ncols)
{
  while(width--) {
    if (*row >= ncols) {
      *row = 0;	/* FIXME: Maybe a message? */
    }
    ++row;
  }
}

static void
parseLines(FILE * fil, guint8 * dst,
           gint width, gint hereheight,
           const ILBMbmhd * bmhd, const ILBMdest * dest,
           const grayval * cmap, gint ncols,
           guint32 viewModes, const grayval * grayTrans,
           IffID ftype)
{
  guint8 *destline = g_new(guint8, width);  /* +1? */
  guint8 *bitlinebuf = g_new(guint8, BYTEPL(width));
  /* FIXME: check both */
  if ((ID_RGB8 == ftype) || (ID_RGBN == ftype)) {
    gboolean success = unpackRGBN8(fil, (grayval *) dst, width * hereheight, ftype, bmhd->nPlanes == 13);
    dst += width * hereheight * ((13 == bmhd->nPlanes) ? byteppRGBA : byteppRGB);
    /* This way alpha doesn't work with RGB8 */
  } else if (!cmap) {
    /* RGB(A) */
    if (VERBOSE)
      fputs("Creating RGB(A)\n", stdout);
    while (hereheight--) {
      gint rgb;
      for (rgb = 0; rgb < byteppRGB; ++rgb) {
        gint bitnr;
        (void) memset(destline, 0, width);
        for (bitnr = 0; bitnr < bitppGray; ++bitnr) {
          readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
          unpackBits(bitlinebuf, destline, bitnr, width);
        }
        writeRGB(destline, dst + rgb, width,
                 byteppRGB + (bmhd->masking != mskNone));
      }
      if (bmhd->masking != mskNone) {
        switch (bmhd->masking) {
          case mskHasMask:
            readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
            bitExpandStep(dst + byteppRGB, bitlinebuf, width, byteppRGBA);
            break;
          case mskHasTransparentColor:
            /* Impossible in non-indexed images */
            /* Hrm.. maybe lookup in palette and use this? FIXME */
            fputs("mskHasTransparentColor in non-indexed image.\n", stderr);
          default:
            {
              gint i;
              for (i = 0; i < width; ++i)
                dst[byteppRGB + i * byteppRGBA] = opaque;
            }
            break;
        }
      }
      dst += width * (byteppRGB + (bmhd->masking != mskNone));
    }
  } else if (!(viewModes & ham)) {
    /* indexed */
    while (hereheight--) {
      gint bitnr;
if(ID_PBM_==ftype) {
/* Chunky */
/* We always read 1 byte per pixel, so "width" bytes. */
readPlaneRow(fil,destline,width,bmhd->compression);
/* Are there IPBMs with maybe another byte pp for alpha? */
} else {
      (void) memset(destline, 0, width);
      /* todo: should really unpack line-by-line, not plane-by-plane. */
#if 1
      for (bitnr = 0; bitnr < dest->depth; ++bitnr) {
        if (dest->planePick & (1 << bitnr)) {
          /* "put the next source bitplane into this bitplane" */
          readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
          unpackBits(bitlinebuf, destline, bitnr, width);  /* +7/8? */
        } else {
          /* "put the corresponding bit from planeOnOff into this bitplane" */
          /* We ignore dest->planeMask since this is no transparency issue */
          if (dest->planeOnOff & (1 << bitnr)) {
            setBits (destline, bitnr, width);
          } else {
            clrBits (destline, bitnr, width);
          }
        }
      }
#else
      {
        char blbuf[BYTEPL(width) * bmhd->nPlanes];
        readPlaneRow(fil, blbuf, BYTEPL(width) * bmhd->nPlanes, bmhd->compression);
        for (bitnr = 0; bitnr < bmhd->nPlanes; ++bitnr) {
          unpackBits(blbuf + (bitnr * BYTEPL(width)), destline, bitnr, width);
        }
      }
#endif
}
      if (grayTrans)
        transGray(destline, width, grayTrans);

      if (bmhd->masking == mskNone) {
        memcpy(dst, destline, width);  /* no alpha */
      } else {
        writeRGB(destline, dst, width, byteppGrayA);  /* make space for alpha */
        switch (bmhd->masking) {
          case mskHasMask:
            /* Only if image is plane- and per-line-based */
            if (ID_ILBM == ftype) {
              readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
              bitExpandStep(dst + byteppGray, bitlinebuf, width, byteppGrayA);
            }
            break;
          case mskHasTransparentColor:
            {
              gint i;
              guint8 *indx = destline;  /* may read dst */
              guint8 *toalpha = dst + byteppGray;
              for (i = width; i > 0; --i) {
                if (*indx++ == bmhd->transparentColor)
                  *toalpha = transparent;
                else
                  *toalpha = opaque;
                toalpha += byteppGrayA;
              }
            }
            break;
          default:
            {
              gint i;
              for (i = 0; i < width; ++i)
                dst[byteppGray + i * byteppGrayA] = opaque;
            }
            break;
        }
        dst += width;
      }
      /* Check indices for range */
      checkIdxRanges (dst, width, ncols);
      dst += width;
    }
  } else {
    /* HAM */
    while (hereheight--) {
      gint bitnr;
      (void) memset(destline, 0, width);
      for (bitnr = 0; bitnr < bmhd->nPlanes; ++bitnr) {
        readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
        unpackBits(bitlinebuf, destline, bitnr, width);
      }
      deHam(dst, destline, width, bmhd->nPlanes, cmap, bmhd->masking != mskNone);
      if (bmhd->masking != mskNone) {
        switch (bmhd->masking) {
          case mskHasMask:
            readPlaneRow(fil, bitlinebuf, BYTEPL(width), bmhd->compression);
            bitExpandStep(dst + byteppRGB, bitlinebuf, width, byteppRGBA);
            break;
          case mskHasTransparentColor:
            {
              gint i;
              guint8 *indx = destline;
              guint8 *toalpha = dst + byteppRGB;
              for (i = width; i > 0; --i) {
                if (*indx++ == bmhd->transparentColor)
                  *toalpha = transparent;
                else
                  *toalpha = opaque;
                toalpha += byteppRGBA;
              }
            }
            break;
          default:
            {
              gint i;
              for (i = 0; i < width; ++i)
                dst[byteppRGB + i * byteppRGBA] = opaque;
            }
            break;
        }
      }
      dst += width * (byteppRGB + (bmhd->masking != mskNone));
    }
  }
  g_free(bitlinebuf);
  g_free(destline);
}

static void
setCmap(gint32 imageID, const guint8 * cmap, gint ncols)
{
  if (VERBOSE)
    printf("Setting cmap (%d colors)...\n", ncols);
  gimp_image_set_cmap(imageID, (guchar *) cmap, ncols);
}


/**** Loading ****/

#if 0
void
  loadBody();
#endif

gint32
loadImage(const char *filename)
{
  gint32 imageID = -1;
  gboolean succ = TRUE;
  char *name = g_new(char, strlen(filename) + 10);

  if (!name) {
    fputs("Out of memory.\n", stderr);
    return (imageID);
  }
  sprintf(name, "Loading %s:", filename);
  gimp_progress_init(name);
  {
    FILE *fil;
    if (VERBOSE)
      printf("=== Loading %s...\n", filename);
    fil = fopen(filename, "rb");
    succ = succ && (fil != NULL);
    if (!fil) {
      fprintf(stderr, "File %s not found.\n", filename);
    } else {
      IffChunkHeader fhead;
      if ((!iffReadHeader(fil, &fhead)) || (ID_FORM != fhead.id)) {
        succ = FALSE;
        g_warning("No FORM header found.");
      } else {
        IffID hdrid;
        IffID ftype = 0;
        succ = succ && readUlong(fil, &hdrid);
        switch (hdrid) {
          case ID_ILBM:
          case ID_PBM_:
          case ID_RGBN:
          case ID_RGB8:
            ftype = hdrid;
            break;
          default:
            ftype = (IffID) 0;
            break;
        }
        if (!ftype) {
          fprintf(stderr, "No ILBM/RGBN/RGB8 header found.\n");
        } else {
          gint32 hunksize;
          gint32 width = 0, height = 0;
          gint32 layerID = -1;
          GDrawable *drawable;
          GPixelRgn pixelRegion;
          ILBMbmhd bmhd;
          guint8 *cmap = 0;
          guint8 *grayTrans = 0;
          gint ncols = 0;
          ILBMcamg camg =
          {0x00000000};
          ILBMdest dest;
          gboolean hasDest = FALSE;
          gboolean running = 1;
          IffChunkHeader chead;

          bmhd.nPlanes = 0;     /* == not yet used */

          while (running && (iffReadHeader(fil, &chead))) {
            hunksize = ((chead.len + 1) / 2) * 2;
            if (VERBOSE)
              iffDumpHeader(&chead);
            switch (chead.id) {
              case ID_BMHD:
                succ = succ && iffReadDataAuto(fil, bmhd);
                if (succ) {
                  valBMHD(&bmhd);
                  width = bmhd.w;
                  height = bmhd.h;
                  if (VERBOSE)
                    dumpBMHD(&bmhd);
                }
                break;
              case ID_CMAP:
                ncols = chead.len / byteppRGB;
                if (VERBOSE)
                  printf("%d colors in CMAP.\n", ncols);
                cmap = (u_char *) g_new(guint8, ncols * byteppRGB + 1 /*pad */ );
                succ = succ && iffReadData(fil, cmap, hunksize);  /* FIXME: +1 unneeded? */
                break;
              case ID_CAMG:
                succ = succ && iffReadDataAuto(fil, camg);
                if(succ) {
                  valCAMG(&camg);
                  if (VERBOSE) {
                    dumpCAMG(&camg);
                  }
                }               /* FIXME: error */
                break;
              case ID_DPI_:
                {
                  ILBMdpi dpi;
                  succ = succ && iffReadDataAuto(fil, dpi);
                  if (succ) {
                    valDPI(&dpi);
                    if (VERBOSE) {
                      dumpDPI (&dpi);
                    }
                  }             /* FIXME: error */
                }
                break;
              case ID_DEST:
                succ = succ && iffReadDataAuto (fil, dest);
                if (succ) {
                  valDEST (&dest);
                  if (VERBOSE) {
                    dumpDEST (&dest);
                  }
                  hasDest = TRUE;
                }
                break;
              case ID_ANNO:
              case ID__C__:
              case ID_CHRS:
              case ID_NAME:
              case ID_TEXT:
                {
                  guint8 *cont = g_new(guint8, chead.len + 1 + 1);
                  /* FIXME: check */
                  succ = succ && iffReadData(fil, cont, hunksize);
                  if (succ) {
                    if (VERBOSE) {
                      char idstr[5];
                      cont[chead.len] = '\0';
                      idToString(chead.id, idstr);
                      printf("%s: %s\n", idstr, cont);
                    }
                  }             /* FIXME: error */
                  g_free(cont);
                }
                break;
              case ID_BODY:
                if (!succ) {
                  running = FALSE;
                  break;
                }
                {
                  guint tileHeight;  /* guint is the return value */
                  guint8 *buf;
                  gboolean exportRGB, isGray = 0;
                  if (hasDest) {
                    /* mmh, should we take mask in account here? */
                    /* now we assume a mask just to be the "highest" bit */
                    if (bmhd.nPlanes > dest.depth) {
                      g_warning ("More planes in file than allowed by DEST.");
                    }
                    if (numBitsSet (dest.planePick) != bmhd.nPlanes) {
                      g_warning ("Number of planes doesn\'t match bits set in planePick.");
                    }
                  } else {
                    dest.depth = bmhd.nPlanes;
                    dest.planePick = dest.planeMask = (1L << bmhd.nPlanes) - 1;
                    /* hasDest = TRUE; */
                  }
                  /* "Any higher order bits should be ignored" */
                  dest.planePick &= 0xFF;
                  dest.planeOnOff &= 0xFF;
                  dest.planeMask &= 0xFF;

                  /*  Some old (pre-AGA) programs saved HAM6 pictures
                   *  without setting the right (or any) CAMG flags.
                   *  Let's guess then.
                   */
                  if (!(camg.viewModes & ham) && (6 == bmhd.nPlanes)
                      && (16 == ncols)) {
                    if (VERBOSE)
                      printf("Guessing HAM6.\n");
                    camg.viewModes |= ham;
                  }
                  if (cmap &&
                      ((bitppRGB == bmhd.nPlanes) ||
                       (ID_RGBN == ftype) ||
                       (ID_RGB8 == ftype))) {
                    /* Well, I've already seen an 24bit image
                     * with a 32color CMAP. :-/ Ignore it. */
                    g_free (cmap);
                    cmap = NULL;
                  }
                  if (!cmap && (bitppGray == bmhd.nPlanes)) {
                    /* Well, seems to be a grayscale. */
                    cmap = allocGrayscale();
                    ncols = maxGrayshades;
                    isGray = 1;
                    grayTrans = allocGrayKeep();  /* 1:1 */
                  } else if (cmap && !(camg.viewModes & ham)) {
                    isGray = isGrayscale(cmap, ncols);
                    if (isGray) {
                      if (VERBOSE)
                        printf("Promoting to grayscale.\n");
                      grayTrans = allocGrayTrans(cmap, ncols);
                    }
                  }
                  if (VERBOSE)
                    printf("isGray:%d\n", (int) isGray);

                  exportRGB = !cmap || (camg.viewModes & ham);
                  if (!exportRGB && !cmap)
                    fprintf(stderr, "Colormap (CMAP) missing!\n");

                  if (camg.viewModes & extraHalfbrite) {
                    cmap = reallocEhbCmap (cmap, &ncols);
                  }

                  /* !! Caution: Every msk.. needs alpha, but we don't
                     support everyone yet !! */
                  imageID = gimp_image_new(width, height,
                             (exportRGB ? RGB : (isGray ? GRAY : INDEXED)));
                  gimp_image_set_filename(imageID, (char *) filename);

                  if (!exportRGB && cmap && !isGray)
                    setCmap(imageID, cmap, ncols);

                  {
                    gboolean needsAlpha = (bmhd.masking != mskNone) ||
                    ((ID_RGBN == ftype) && (bmhd.nPlanes == 13)) ||
                    ((ID_RGB8 == ftype) && (bmhd.nPlanes == 25));
                    if (VERBOSE)
                      printf("exportRGB:%d needsAlpha:%d\n",
                             (int) exportRGB, (int) needsAlpha);
                    layerID = gimp_layer_new(imageID, "Background",
                                             width, height,
                                             needsAlpha ?
                                             (exportRGB ? RGBA_IMAGE : (isGray ? GRAYA_IMAGE : INDEXEDA_IMAGE))
                                             : (exportRGB ? RGB_IMAGE : (isGray ? GRAY_IMAGE : INDEXED_IMAGE)),
                                             100, NORMAL_MODE);
                    gimp_image_add_layer(imageID, layerID, 0);
                    drawable = gimp_drawable_get(layerID);
                    gimp_pixel_rgn_init(&pixelRegion, drawable, 0, 0,
                                        drawable->width, drawable->height,
                                        TRUE, FALSE);
                    tileHeight = gimp_tile_height();
                    if (VERBOSE)
                      printf("Gimp tileHeight: %d\n", tileHeight);
                    buf = g_new(guint8, tileHeight * width
                                * ((exportRGB ? byteppRGB : byteppGray)
                                   + (needsAlpha ? 1 : 0)));
                  }
                  if (!buf) {
                    fputs ("Buffer allocation\n", stderr);
                    succ = FALSE;
                  } else {
                    guint actTop;
                    gint scanlines;
                    for (actTop = 0; actTop < (guint) height; actTop += tileHeight) {
                      scanlines = MIN(tileHeight, height - actTop);
                      if (VERBOSE)
                        printf("Processing lines%4d upto%4d (%2d)...\n",
                               actTop, actTop + scanlines - 1, scanlines);
                      parseLines(fil, buf, width, scanlines,
                             &bmhd, &dest, cmap, ncols, camg.viewModes, grayTrans, ftype);
                      gimp_progress_update((double) actTop / (double) height);
                      gimp_pixel_rgn_set_rect(&pixelRegion, buf,
                                              0, actTop,
                                              drawable->width, scanlines);
                    }
                    gimp_progress_update(1.0);
                    g_free(buf);
                  }
                  gimp_drawable_flush(drawable);  /* unneccessary? */
                  gimp_drawable_detach(drawable);
                  running = FALSE;  /* Stop parsing IFF */
                }
                break;
              default:
                (void) fseek(fil, hunksize, SEEK_CUR);
                break;
            }
            if (!succ) {
              running = FALSE;
            }
          }
          if (cmap) {
            if (grayTrans)
              g_free(grayTrans);
            g_free(cmap);
          }
        }
      }
      (void) fclose(fil);
    }
  }
  g_free(name);
  return (imageID);
}

/***** Saving *****/


static void
extractBits(guint8 * dest, const guint8 * src, gint bitnr,
            gint32 width, gint skipAmount)
{
  guint8 mask = 1 << bitnr;
  guint8 ch = 0;
  guint8 actbit = 1 << 7;
  while (width--) {
    if (*src & mask)
      ch |= actbit;
    actbit >>= 1;
    if (!actbit) {
      actbit = 1 << 7;
      *dest++ = ch;
      ch = 0;
    }
    src += skipAmount;
  }
  if (actbit != (1 << 7))
    *dest = ch;
}

static void
extractAlpha(guint8 * dest, const guint8 * src, gint32 width,
             gint skipAmount, guint8 threshold)
{
  guint8 destch = 0;
  guint8 bit = 1 << 7;
  while (width--) {
    if (*src >= threshold)
      destch |= bit;
    bit >>= 1;
    if (!bit) {
      *dest++ = destch;
      destch = 0;
      bit = 1 << 7;
    }
    src += skipAmount;
  }
  if (bit != (1 << 7))
    *dest = destch;
}

/*  Returns the number of planes that would be
 *  (at least) needed to store the given number
 *  of colors
 */

static gint
calcPlanes(gint ncols)
{
  gint planes = 1;
  gint powr = 2;
  while (powr < ncols) {
    ++planes;
    powr <<= 1;
  }
  return (planes);
}

/*  Write CMAP header to file, using the given
 *  palette of the given number of colors.
 */

static gint32
writeCMAP(FILE * fil, const grayval * cmap, gint palWidth)
{
  gint32 written = 0;
  IffChunkHeader chead;
  iffInitHeader (&chead, ID_CMAP, byteppRGB * palWidth);
  if (VERBOSE)
    printf("Saving CMAP...\n");
  if (iffWriteHeader(fil, &chead) &&
      iffWriteData(fil, cmap, written = byteppRGB * palWidth)
    ) {
    written += 8;
    if ((byteppRGB * palWidth) & 1) {
      if ((writeUchar(fil, (grayval) transparent)))
        ++written;
      else
        written = 0;
    }
  } else
    written = 0;
  if (!written)
    perror("writeCMAP()");
  return (written);
}

gint
saveImage(const char *filename,
          gint32 imageID,
          gint32 drawableID)
{
  gint rc = FALSE;
  gboolean succ = TRUE;
  guint16 alpha = 0;
  guint16 compress = ilbmvals.compress ? cmpByteRun1 : cmpNone;
  guint16 saveham = ilbmvals.save_ham;
  gboolean outGrayscale = FALSE;
  const guint8 *cmap = 0;
  gint ncols;
  gint dtype;
  char *name;

  dtype = gimp_drawable_type(drawableID);
  switch (dtype) {
    case GRAYA_IMAGE:
      outGrayscale = TRUE;
    case RGBA_IMAGE:
      alpha = 1;
      break;
    case GRAY_IMAGE:
      outGrayscale = TRUE;
    case RGB_IMAGE:
      break;
    case INDEXEDA_IMAGE:
      alpha = 1;
    case INDEXED_IMAGE:
      cmap = gimp_image_get_cmap(imageID, &ncols);
      break;
    default:
      return (FALSE);           /* rc? */
      break;
  }

  name = g_new(char, strlen(filename) + 9);
  if (!name) {
    fputs("Out of memory.\n", stderr);
    return (rc);
  }
  sprintf(name, "Saving %s:", filename);
  gimp_progress_init(name);

  /* save... */
  {
    GDrawable *drawable = gimp_drawable_get(drawableID);
    gint width = drawable->width, height = drawable->height;
    FILE *fil;
    gint bytepp = drawable->bpp;  /* includes alpha? */
    /* Above: compression, what mask mode to map alpha to, */

    gint nPlanes = 0;
    /* output HAM, output EHB */
    gint outHAM = 0;
    const guint8 *outCmap = 0;  /* cmap to write, if any */
    gboolean outIndexed = 0;    /* write indexed image, or RGBx */
    /*gboolean outGrayscale = 0; *//* Output should be grayscale */
    gint outNcols = 0;          /* number of colors in cmap */
    gint outNplanes = 0;        /* number of planes to write */
    gint outColorbits = 0;      /* bits per color */
    gint outAlphabits = 0;      /* if alpha, how many bits */
    IffID ftype = ID_ILBM;      /* file format to use */

    /* IMPORTANT: cmap can only be != 0 if the image is neither RGB nor GRAY(!) */

    if (NULL != cmap) {
      nPlanes = calcPlanes(ncols);
    } else {
      /* okay at 24bit with alpha (==32bit)? */
      nPlanes = (bytepp - alpha) * bitppGray;
    }

    if (saveham) {
      if ((dtype == RGB_IMAGE) || (dtype == RGBA_IMAGE)) {  /* Doesn't have to be that rigid */
        outHAM = 6;
      }
    }
    if (alpha) {
      outAlphabits = 1;
    } else {
      outAlphabits = 0;
    }

    if (VERBOSE)
      printf("=== Saving %s...\n", filename);
    fil = fopen(filename, "wb");
    if (!fil)
      fprintf(stderr, "Cannot open file %s for writing.\n", filename);
    else {
      /* Always includes one or two EOS characters */
      ILBMbmhd bmhd;
      ILBMcamg camg = {0};
      guint32 totsize = 0, bodysize;
      guint32 bodyLenOff;
      IffChunkHeader fhead, chead;
      gboolean freeCmap;

/**** FORM xxxx ILBM ****/

      iffInitHeader(&fhead, ID_FORM, ntohl(0x0BADC0DE));
      succ = succ && iffWriteHeader(fil, &fhead);
      succ = succ && writeUlong(fil, ID_ILBM);
      totsize += 4;

/**** ANNO ****/
#define PLUGID "Written by the Gimp ILBM plugin "VERSION"\0"
#define PLUGSIZE (sizeof(PLUGID) & ~1)
      iffInitHeader(&chead, ID_ANNO, PLUGSIZE);
      succ = succ && iffWriteHeader(fil, &chead);
      succ = succ && iffWriteData(fil, PLUGID, PLUGSIZE);
      totsize += 8 + PLUGSIZE;

      /**** BMHD ****/

      iffInitHeader(&chead, ID_BMHD, sizeof(ILBMbmhd));
      succ = succ && iffWriteHeader(fil, &chead);

      if (VERBOSE)
        printf("bytepp:%d, nPlanes:%d, ncols:%ld, alpha:%d\n",
               bytepp, nPlanes, cmap ? ncols : (1L << nPlanes), alpha);
      genBMHD(&bmhd, width, height, nPlanes);
      if (alpha)
        bmhd.masking = mskHasMask;
      if (compress)
        bmhd.compression = compress;
      if (outHAM)
        bmhd.nPlanes = outHAM;
      succ = succ && iffWriteDataAuto(fil, bmhd);
      totsize += 8 + sizeof(ILBMbmhd);

      /**** CMAP ****/

      freeCmap = FALSE;

      if (outHAM) {
        /* Either picture is 24planes or user wished to save in HAM */
        /* So save HAM (write CAMG HAM chunk) */
        /* Use pre-defined HAM optimized palette */
        camg.viewModes |= ham;
        outCmap = hamPal;
        outNcols = sizeof(hamPal) / byteppRGB;
      } else if (cmap) {
        /* Image is indexed; use palette given by the Gimp */
        outCmap = cmap;
        outNcols = ncols;
      } else if ((dtype == GRAY_IMAGE) || (dtype == GRAYA_IMAGE)) {
        /* Gimp gave us a grayscale image, save as indexed */
        outCmap = allocGrayscale();
        outNcols = maxGrayshades;
        freeCmap = TRUE;
      }
      if (NULL != outCmap) {
        totsize += writeCMAP(fil, outCmap, outNcols);
        if (freeCmap) {
          g_free((guint8 *) outCmap);
        }
      }

      /**** CAMG ****/

      /* Pictures with 640 pixels width and more are usually HighRes */
      if (width >= 640) {
        camg.viewModes |= hiRes;
      }
      /* Pictures with 400 pixels height and more are usually Interlaced */
      if (height >= 400) {
        camg.viewModes |= lace;
      }

      if (camg.viewModes) {
        iffInitHeader(&chead, ID_CAMG, sizeof(ILBMcamg));
        succ = succ && iffWriteHeader(fil, &chead);
        succ = succ && writeUlong(fil, camg.viewModes);	/* FIXME */
        totsize += 8 + sizeof(ILBMcamg);
      }

      /**** BODY ****/

      bodyLenOff = ftell(fil) + 4;  /* FIXME */
      if (VERBOSE)
        printf("bodyLenOff:%d\n", bodyLenOff);
      iffInitHeader(&chead, ID_BODY, ntohl(0x0BADC0DE));
      succ = succ && iffWriteHeader(fil, &chead);
      {
        gint threshold = (gint) (opaque * ilbmvals.threshold);
        gint32 tileHeight = gimp_tile_height();
        guint8 *buffer;

        buffer = g_new(guint8, tileHeight * width * bytepp);
        if (DEBUG)
          printf("Using a buffer of %ld byte.\n", (long) tileHeight * width * bytepp);
        if (buffer) {
          gint actTop;
          GPixelRgn pixelRegion;
          guint8 *packedBuf = allocPackedBuf(BYTEPL(width), compress);
          guint8 *plane = g_new(guint8, BYTEPL(width));
          /* FIXME: check */
          gimp_pixel_rgn_init(&pixelRegion, drawable,
                              0, 0, width, height,
                              TRUE, FALSE /* ? */ );
          for (actTop = 0; actTop < height; actTop += tileHeight) {
            guint8 *data;
            gint scanlines;
            scanlines = MIN(tileHeight, height - actTop);
            if (VERBOSE)
              printf("Processing lines%4d upto%4d (%2d)...\n",
                     actTop, actTop + scanlines - 1, scanlines);
            gimp_pixel_rgn_get_rect(&pixelRegion, buffer,
                                    0, actTop, width, scanlines);
            data = buffer;

            if (!outCmap) {
              while (scanlines--) {
                /* 24bit is saved r0..r7g0..g7b0..b7 */
                gint rgb;
                for (rgb = 0; rgb < (bytepp - alpha); ++rgb) {
                  gint bitnr;
                  for (bitnr = 0; bitnr < bitppGray; ++bitnr) {
                    extractBits(plane, data + rgb, bitnr, width, bytepp);
                    writePlaneRow(fil, plane, BYTEPL(width), compress, packedBuf);
                  }
                }
                if (alpha) {
                  extractAlpha(plane, data + bytepp - byteppGray,
                               width, bytepp, threshold);
                  writePlaneRow(fil, plane, BYTEPL(width), compress, packedBuf);
                }
                data += width * bytepp;
              }
            } else {
              /* indexed or grayscale */
              while (scanlines--) {
                gint bitnr;
                for (bitnr = 0; bitnr < nPlanes; ++bitnr) {
                  /* Write planes 0..x */
                  extractBits(plane, data, bitnr, width,
                              byteppGray + alpha);
                  writePlaneRow(fil, plane, BYTEPL(width), compress, packedBuf);
                }
                if (alpha) {
                  /* Write the extra 1 bit alpha mask per line */
                  extractAlpha(plane, data + byteppGray,
                               width, byteppGrayA, threshold);
                  writePlaneRow(fil, plane, BYTEPL(width), compress, packedBuf);
                  data += width;
                }
                data += width;
              }
            }
            gimp_progress_update((double) actTop / (double) height);
          }
          freePackedBuf(packedBuf);

          bodysize = ftell(fil) - bodyLenOff + 4;
          if (VERBOSE)
            printf("bodysize:%lu\n", (unsigned long) bodysize);

          if (bodysize & 1) {
            /* Add padding */
            succ = succ && writeUchar(fil, '\0');
          }
          succ = succ && writeLongAt(fil, bodysize, bodyLenOff);

          bodysize = (bodysize + 1) / 2 * 2;
          totsize += 8 + bodysize;
          succ = succ && writeLongAt(fil, totsize, 4);

          if (succ) {
            rc = TRUE;
          }
          g_free(plane);
          g_free(buffer);
          gimp_progress_update(1.0);
        } else {                /* buffer==0 */
        }
      }
      if (0 != fclose(fil)) {
        perror("fclose()");
        rc = FALSE;
      }
      if (!rc) {
        remove(filename);
        fputs("Save failed, output file removed.\n", stderr);
      }
    }
    gimp_drawable_detach(drawable);
  }
  g_free(name);
  return (rc);
}
