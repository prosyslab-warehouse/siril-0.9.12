/*
 * This file is part of Siril, an astronomy image processor.
 * Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)
 * Copyright (C) 2012-2019 team free-astro (see more in AUTHORS file)
 * Reference site is https://free-astro.org/index.php/Siril
 *
 * Siril is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Siril is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Siril. If not, see <http://www.gnu.org/licenses/>.
 */

/* Internal image formats import and export: BMP and PPM */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "core/siril.h"
#include "core/proto.h"
#include "gui/callbacks.h"
#include "gui/progress_and_log.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int bmp32tofits48(unsigned char *rvb, unsigned long rx, unsigned long ry, fits *fit) {
	unsigned long datasize;
	int i, j;
	WORD *rdata, *gdata, *bdata, *olddata;

	datasize = rx * ry;

	olddata = fit->data;
	if ((fit->data = realloc(fit->data, 3 * datasize * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(fit->data);
		return 1;
	}

	rdata = fit->pdata[RLAYER] = fit->data;
	gdata = fit->pdata[GLAYER] = fit->data + datasize;
	bdata = fit->pdata[BLAYER] = fit->data + 2 * datasize;
	for (i = 0; i < ry; i++) {
		for (j = 0; j < rx; j++) {
			*bdata++ = (WORD) *rvb++;
			*gdata++ = (WORD) *rvb++;
			*rdata++ = (WORD) *rvb++;
			rvb++;
		}
	}
	fit->bitpix = fit->orig_bitpix = BYTE_IMG;
	fit->naxis = 3;
	fit->rx = rx;
	fit->ry = ry;
	fit->naxes[0] = rx;
	fit->naxes[1] = ry;
	fit->naxes[2] = 3;
	fit->binning_x = fit->binning_y = 1;
	return 0;
}

static int bmp24tofits48(unsigned char *rvb, unsigned long rx, unsigned long ry, fits *fit) {
	int i, j;
	WORD *rdata, *gdata, *bdata, *olddata;

	int padsize = (4 - (rx * 3) % 4) % 4;
	int newdatasize = ry * rx;

	olddata = fit->data;
	if ((fit->data = realloc(fit->data, 3 * newdatasize * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(fit->data);
		return 1;
	}
	rdata = fit->pdata[RLAYER] = fit->data;
	gdata = fit->pdata[GLAYER] = fit->data + newdatasize;
	bdata = fit->pdata[BLAYER] = fit->data + 2 * newdatasize;
	for (i = 0; i < ry; i++) {
		for (j = 0; j < rx; j++) {
			*bdata++ = *rvb++;
			*gdata++ = *rvb++;
			*rdata++ = *rvb++;
		}
		rvb += padsize;
	}
	fit->bitpix = fit->orig_bitpix = BYTE_IMG;
	fit->naxis = 3;
	fit->rx = rx;
	fit->ry = ry;
	fit->naxes[0] = rx;
	fit->naxes[1] = ry;
	fit->naxes[2] = 3;
	fit->binning_x = fit->binning_y = 1;
	return 0;
}

static int bmp16tofits48(unsigned char *rvb, unsigned long rx, unsigned long ry, fits *fit) {
	int i;
	WORD *rdata, *gdata, *bdata, *olddata;

	int newdatasize = ry * rx;

	olddata = fit->data;
	if ((fit->data = realloc(fit->data, 3 * newdatasize * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(fit->data);
		return 1;
	}
	rdata = fit->pdata[RLAYER] = fit->data;
	gdata = fit->pdata[GLAYER] = fit->data + newdatasize;
	bdata = fit->pdata[BLAYER] = fit->data + 2 * newdatasize;
	for (i = 0; i < ry * rx; i++) {
		unsigned char buf0 = *rvb++;
		unsigned char buf1 = *rvb++;
		unsigned pixel_data = buf0 | buf1 << 8;

        *rdata++ = ((pixel_data & 0x7c00) >> 10) * 255.0 / 31.0 + 0.5;
        *gdata++ = ((pixel_data & 0x03e0) >> 5) * 255.0 / 31.0 + 0.5;
        *bdata++ = ((pixel_data & 0x001f) >> 0) * 255.0 / 31.0 + 0.5;

	}
	fit->bitpix = fit->orig_bitpix = BYTE_IMG;
	fit->naxis = 3;
	fit->rx = rx;
	fit->ry = ry;
	fit->naxes[0] = rx;
	fit->naxes[1] = ry;
	fit->naxes[2] = 3;
	fit->binning_x = fit->binning_y = 1;
	return 0;
}

static int bmp8tofits(unsigned char *rgb, unsigned long rx, unsigned long ry, fits *fit) {
	unsigned long nbdata, padsize;
	int i, j;
	WORD *data, *olddata;

	padsize = (4 - (rx % 4)) % 4;
	nbdata = rx * ry;

	olddata = fit->data;
	if ((fit->data = realloc(fit->data, nbdata * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(fit->data);
		return 1;
	}
	data = fit->pdata[BW_LAYER] = fit->data;
	for (i = 0; i < ry; i++) {
		for (j = 0; j < rx; j++) {
			*data++ = (WORD) *rgb++;
		}
		rgb += padsize;
	}
	fit->bitpix = fit->orig_bitpix = BYTE_IMG;
	fit->rx = rx;
	fit->ry = ry;
	fit->naxes[0] = rx;
	fit->naxes[1] = ry;
	fit->naxes[2] = 1;
	fit->naxis = 2;
	fit->binning_x = fit->binning_y = 1;
	return 0;
}

static void get_image_size(BYTE *header, unsigned long *width,
		unsigned long *height) {
	unsigned long bitmapinfoheader = 0;
	unsigned long lx = 0, ly = 0;
	unsigned short sx = 0, sy = 0;

	memcpy(&bitmapinfoheader, header + 14, 4);
	if (bitmapinfoheader == 12) {
		memcpy(&sx, header + 18, 2);
		memcpy(&sy, header + 20, 2);
		*width = (unsigned long) sx;
		*height = (unsigned long) sy;
	} else {
		memcpy(&lx, header + 18, 4);
		memcpy(&ly, header + 22, 4);
		*width = lx;
		*height = ly;
	}
}

/* reads a BMP image at filename `name', and stores it into the fit argument */
int readbmp(const char *name, fits *fit) {
	BYTE header[256];
	FILE *file;
	long int count;
	unsigned char *buf;
	unsigned long data_offset = 0;
	unsigned long width = 0, height = 0;
	unsigned long nbdata, padsize;
	unsigned short nbplane = 0;

	if ((file = g_fopen(name, "rb")) == NULL) {
		siril_log_message(_("Error opening BMP.\n"));
		return -1;
	}

	if ((count = fread(header, 1, 54, file)) != 54) {
		fprintf(stderr, "readbmp: %ld header bytes read instead of 54\n", count);
		perror("readbmp");
		fclose(file);
		return -1;
	}

/*	memcpy(&compression, header + 30, 4);*/

	get_image_size(header, &width, &height);
	memcpy(&nbplane, header + 28, 2);
	nbplane = nbplane / 8;
	memcpy(&data_offset, header + 10, 4);

	padsize = (4 - (width * nbplane) % 4) % 4;
	nbdata = width * height * nbplane + height * padsize;

	if (fseek(file, data_offset, SEEK_SET) == -1) {
		perror("BMP fseek for data");
		fclose(file);
		return -1;
	}

	buf = malloc(nbdata);
	if (!buf) {
		PRINT_ALLOC_ERR;
		fclose(file);
		return -1;
	}
	unsigned long f;
	if (nbdata != (f = fread(buf, 1, nbdata, file))) {
		fprintf(stderr, "readbmp: could not read all data: (%lud, %lud)\n", nbdata, f);
		free(buf);
		fclose(file);
		return -1;
	}
	fclose(file);

	switch (nbplane) {
	case 1:
		bmp8tofits(buf, width, height, fit);
		break;
	case 2:
		bmp16tofits48(buf, width, height, fit);
		break;
	case 3:
		bmp24tofits48(buf, width, height, fit);
		break;
	case 4:
		bmp32tofits48(buf, width, height, fit);
		break;
	default:
		siril_log_message(_("Sorry but Siril cannot "
				"open this kind of BMP. Try to convert it before.\n"));
	}
	free(buf);
	char *basename = g_path_get_basename(name);
	siril_log_message(_("Reading BMP: file %s, %ld layer(s), %ux%u pixels\n"),
			basename, fit->naxes[2], fit->rx, fit->ry);
	g_free(basename);
	return (int) nbplane;
}

int savebmp(const char *name, fits *fit) {
	unsigned char bmpfileheader[14] = { 'B', 'M', 	//Magic Number
			0, 0, 0, 0, 	//Size in bytes, see below
			0, 0, 0, 0, 54, 0, 0, 0	//offset
			};
	unsigned char bmpinfoheader[40] = { 40, 0, 0, 0, //info of the header size
			0, 0, 0, 0, 	//width, see below
			0, 0, 0, 0, 	//height, see below
			1, 0, 		//number color planes
			24, 0,		//bits per pixel
			0, 0, 0, 0, 	//no compression
			0, 0, 0, 0, 	//image bits size
			0, 0, 0, 0, 	//horizontal resolution, we don't care
			0, 0, 0, 0, 	//vertical resolution, we don't care neither
			0, 0, 0, 0, 	//colors in pallete
			0, 0, 0, 0, 	//important colors
			};
	unsigned int width = fit->rx, height = fit->ry;
	double norm;

	FILE *f;

	WORD *gbuf[3] = { fit->pdata[RLAYER], fit->pdata[GLAYER], fit->pdata[BLAYER] };

	int padsize = (4 - (width * 3) % 4) % 4;
	int datasize = width * height * 3 + padsize * height;
	int filesize = datasize + sizeof(bmpfileheader) + sizeof(bmpinfoheader);
	int i, j;
	WORD red, blue, green;
	unsigned char pixel[3];

	bmpfileheader[2] = (unsigned char) (filesize);
	bmpfileheader[3] = (unsigned char) (filesize >> 8);
	bmpfileheader[4] = (unsigned char) (filesize >> 16);
	bmpfileheader[5] = (unsigned char) (filesize >> 24);

	bmpinfoheader[4] = (unsigned char) (width);
	bmpinfoheader[5] = (unsigned char) (width >> 8);
	bmpinfoheader[6] = (unsigned char) (width >> 16);
	bmpinfoheader[7] = (unsigned char) (width >> 24);

	bmpinfoheader[8] = (unsigned char) (height);
	bmpinfoheader[9] = (unsigned char) (height >> 8);
	bmpinfoheader[10] = (unsigned char) (height >> 16);
	bmpinfoheader[11] = (unsigned char) (height >> 24);

	bmpinfoheader[24] = (unsigned char) (datasize);
	bmpinfoheader[25] = (unsigned char) (datasize >> 8);
	bmpinfoheader[26] = (unsigned char) (datasize >> 16);
	bmpinfoheader[27] = (unsigned char) (datasize >> 24);

	char *filename = strdup(name);
	if (!ends_with(filename, ".bmp")) {
		filename = str_append(&filename, ".bmp");
	}

	f = g_fopen(filename, "wb");
	if (f == NULL) {
		siril_log_message(_("Can't create BMP file.\n"));
		free(filename);
		return 1;
	}

	norm = (fit->orig_bitpix != BYTE_IMG ?
			UCHAR_MAX_DOUBLE / USHRT_MAX_DOUBLE : 1.0);

	fwrite(bmpfileheader, sizeof(bmpfileheader), 1, f);
	fwrite(bmpinfoheader, sizeof(bmpinfoheader), 1, f);

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			red = *gbuf[RLAYER]++;
			if (fit->naxes[2] == 3) {
				green = *gbuf[GLAYER]++;
				blue = *gbuf[BLAYER]++;
			} else {
				green = red;
				blue = red;
			}

			pixel[0] = round_to_BYTE(blue * norm); /* swap Blue and Red */
			pixel[1] = round_to_BYTE(green * norm);
			pixel[2] = round_to_BYTE(red * norm);

			fwrite(pixel, sizeof(pixel), 1, f);
		}
		if (padsize != 0)
			fwrite("0", 1, padsize, f);		//We fill the end of width with 0
	}
	fclose(f);
	siril_log_message(_("Saving BMP: file %s, %ld layer(s), %ux%u pixels\n"), filename,
			fit->naxes[2], fit->rx, fit->ry);
	free(filename);
	return 0;
}

/********************* NetPBM IMAGE LOADING **********************/
/* P1	Portable bitmap	ASCII
 * P2	Portable graymap	ASCII
 * P3	Portable pixmap	ASCII
 * P4	Portable bitmap	Binary
 * P5	Portable graymap	Binary
 * P6	Portable pixmap	Binary
 */
/* This method loads a pnm or pgm binary file into the fits image passed as argument. */
int import_pnm_to_fits(const char *filename, fits *fit) {
	FILE *file;
	char buf[256];
	int i, j, max_val;
	size_t stride;

	if ((file = g_fopen(filename, "rb")) == NULL) {
		siril_log_message(_("Sorry but Siril cannot open this file.\n"));
		return -1;
	}
	if (fgets(buf, 256, file) == NULL) {
		perror("reading pnm file");
		fclose(file);
		return -1;
	}
	if (buf[0] != 'P' || buf[1] < '5' || buf[1] > '6' || buf[2] != '\n') {
		siril_log_message(
				_("Wrong magic cookie in PNM file, ASCII types and"
						" b&w bitmaps are not supported.\n"));
		fclose(file);
		return -1;
	}
	if (buf[1] == '6') {
		fit->naxis = 3;
		fit->naxes[2] = 3;
	} else {
		fit->naxes[2] = 1;
		fit->naxis = 2;
	}

	do {
		if (fgets(buf, 256, file) == NULL) {
			fclose(file);
			return -1;
		}
	} while (buf[0] == '#');
	i = 0;
	while (buf[i] >= '0' && buf[i] <= '9')
		i++;
	if (i == 0) {
		fclose(file);
		return -1;
	}
	buf[i] = '\0';
	fit->rx = atoi(buf);
	j = ++i;
	while (buf[j] >= '0' && buf[j] <= '9')
		j++;
	if (j == i) {
		fclose(file);
		return -1;
	}
	if (buf[j] != '\n') {
		fclose(file);
		return -1;
	}
	buf[j] = '\0';
	fit->ry = atoi(buf + i);

	do {
		if (fgets(buf, 256, file) == NULL) {
			fclose(file);
			return -1;
		}
	} while (buf[0] == '#');
	i = 0;
	while (buf[i] >= '0' && buf[i] <= '9')
		i++;
	if (buf[i] != '\n') {
		fclose(file);
		return -1;
	}
	buf[i] = '\0';
	max_val = atoi(buf);
	if (max_val < UCHAR_MAX) {
		fclose(file);
		return -1;
	}
	if (max_val == UCHAR_MAX) {
		/* 8-bit file */
		unsigned char *tmpbuf = NULL;
		WORD *olddata;
		if (fit->naxes[2] == 1)
			stride = fit->rx;
		else
			stride = fit->rx * 3;
		tmpbuf = malloc(stride * fit->ry);
		olddata = fit->data;
		fit->data = realloc(fit->data, stride * fit->ry * sizeof(WORD));
		if (fit->data == NULL || tmpbuf == NULL) {
			PRINT_ALLOC_ERR;
			fclose(file);
			if (olddata && !fit->data)
				free(olddata);
			if (tmpbuf)
				free(tmpbuf);
			fit->data = NULL;
			return -1;
		}
		if (fread(tmpbuf, stride, fit->ry, file) < fit->ry) {
			siril_log_message(_("Error reading 8-bit PPM image data.\n"));
			fclose(file);
			free(tmpbuf);
			free(fit->data);
			fit->data = NULL;
			return -1;
		}
		if (fit->naxes[2] == 3)
			rgb24bit_to_fits48bit(tmpbuf, fit, FALSE);
		else
			rgb8bit_to_fits16bit(tmpbuf, fit);
		free(tmpbuf);
		fit->bitpix = BYTE_IMG;
		fit->binning_x = fit->binning_y = 1;
		fits_flip_top_to_bottom(fit);
	} else if (max_val == USHRT_MAX || max_val == SHRT_MAX) {
		/* 16-bit file */
		if (fit->naxes[2] == 1) {
			int nbdata;
			WORD *olddata = fit->data;
			stride = fit->rx * sizeof(WORD);
			fit->data = realloc(fit->data, stride * fit->ry * sizeof(WORD));
			if (fit->data == NULL) {
				PRINT_ALLOC_ERR;
				fclose(file);
				if (olddata)
					free(olddata);
				return -1;
			}
			if (fread(fit->data, stride, fit->ry, file) < fit->ry) {
				siril_log_message(
						_("Error reading 16-bit gray PPM image data.\n"));
				fclose(file);
				free(fit->data);
				fit->data = NULL;
				return -1;
			}
			/* change endianness in place */
			nbdata = fit->rx * fit->ry;
			for (i = 0; i < nbdata; i++)
				fit->data[i] = (fit->data[i] >> 8) | (fit->data[i] << 8);
			fit->pdata[0] = fit->data;
			fit->pdata[1] = fit->data;
			fit->pdata[2] = fit->data;

		} else {
			/* RGB 16-bit image */
			WORD *tmpbuf, *olddata = fit->data;
			stride = fit->rx * 3 * sizeof(WORD);
			tmpbuf = malloc(stride * fit->ry);
			fit->data = realloc(fit->data, stride * fit->ry * sizeof(WORD));
			if (fit->data == NULL || tmpbuf == NULL) {
				PRINT_ALLOC_ERR;
				fclose(file);
				if (olddata && !fit->data)
					free(olddata);
				if (tmpbuf)
					free(tmpbuf);
				fit->data = NULL;
				return -1;
			}
			if (fread(tmpbuf, stride, fit->ry, file) < fit->ry) {
				siril_log_message(
						_("Error reading 16-bit color PPM image data.\n"));
				fclose(file);
				free(tmpbuf);
				free(fit->data);
				fit->data = NULL;
				return -1;
			}
			rgb48bit_to_fits48bit(tmpbuf, fit, FALSE, TRUE);
			free(tmpbuf);
		}
		fit->bitpix = USHORT_IMG;
		fit->binning_x = fit->binning_y = 1;
		fits_flip_top_to_bottom(fit);
	} else {
		siril_log_message(_("Not handled max value for PNM: %d.\n"),
				max_val);
		fclose(file);
		return -1;
	}
	fclose(file);
	char *basename = g_path_get_basename(filename);
	siril_log_message(_("Reading NetPBM: file %s, %ld layer(s), %ux%u pixels\n"),
			basename, fit->naxes[2], fit->rx, fit->ry);
	g_free(basename);
	return fit->naxes[2];
}

static int saveppm(const char *name, fits *fit) {
	FILE *fp = g_fopen(name, "wb");
	int i;
	int ndata = fit->rx * fit->ry;
	double norm;
	const char *comment = "# CREATOR : SIRIL";

	fprintf(fp, "P6\n%s\n%u %u\n%u\n", comment, fit->rx, fit->ry, USHRT_MAX);
	WORD *gbuf[3] =
			{ fit->pdata[RLAYER], fit->pdata[GLAYER], fit->pdata[BLAYER] };
	fits_flip_top_to_bottom(fit);
	norm = (fit->orig_bitpix != BYTE_IMG) ? 1.0 : USHRT_MAX_DOUBLE / UCHAR_MAX_DOUBLE;
	for (i = 0; i < ndata; i++) {
		WORD color[3];
		color[0] = *gbuf[RLAYER]++ * norm;
		color[1] = *gbuf[GLAYER]++ * norm;
		color[2] = *gbuf[BLAYER]++ * norm;

		/* change endianness in place */
		/* FIX ME : For a small amount of files (for example,
		 * jpg converted to fit with iris),
		 * this swap is not required and causes bad image 
		 * THIS CASE SHOULD NOT BE VERY FREQUENT */

		color[0] = (color[0] >> 8) | (color[0] << 8);
		color[1] = (color[1] >> 8) | (color[1] << 8);
		color[2] = (color[2] >> 8) | (color[2] << 8);
		fwrite(color, sizeof(WORD), 3, fp);
	}
	fclose(fp);
	fits_flip_top_to_bottom(fit);
	siril_log_message(_("Saving NetPBM: file %s, %ld layer(s), %ux%u pixels\n"),
			name, fit->naxes[2], fit->rx, fit->ry);
	return 0;
}

static int savepgm(const char *name, fits *fit) {
	FILE *fp;
	int i;
	int ndata = fit->rx * fit->ry;
	double norm;
	WORD *gbuf = fit->pdata[RLAYER];
	const char *comment = "# CREATOR : SIRIL";

	fp = g_fopen(name, "wb");
	if (!fp)
		return -1;
	fprintf(fp, "P5\n%s\n%u %u\n%u\n", comment, fit->rx, fit->ry, USHRT_MAX);

	fits_flip_top_to_bottom(fit);
	norm = (fit->orig_bitpix != BYTE_IMG) ? 1.0 : USHRT_MAX_DOUBLE / UCHAR_MAX_DOUBLE;
	for (i = 0; i < ndata; i++) {
		WORD tmp = *gbuf++ * norm;
		/* change endianness in place */
		WORD data[1];
		data[0] = (tmp >> 8) | (tmp << 8);
		fwrite(data, sizeof(data), 1, fp);
	}
	fclose(fp);
	fits_flip_top_to_bottom(fit);
	siril_log_message(_("Saving NetPBM: file %s, %ld layer(s), %ux%u pixels\n"),
			name, fit->naxes[2], fit->rx, fit->ry);
	return 0;
}

int saveNetPBM(const char *name, fits *fit) {
	int retval;
	char *filename = strdup(name);

	if (fit->naxes[2] == 1) {
		if (!ends_with(filename, ".pgm")) {
			filename = str_append(&filename, ".pgm");
		}
		retval = savepgm(filename, fit);
	} else {
		if (!ends_with(filename, ".ppm") && !ends_with(filename, ".pnm")) {
			filename = str_append(&filename, ".ppm");
		}
		retval = saveppm(filename, fit);
	}
	free(filename);
	return retval;
}


static int pictofit(WORD *buf, fits *fit) {
	int nbdata;
	int i;
	WORD *data, *olddata = fit->data;

	nbdata = fit->rx * fit->ry;
	if ((fit->data = realloc(fit->data, nbdata * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(olddata);
		return -1;
	}
	data = fit->pdata[BW_LAYER] = fit->data;
	fit->pdata[GLAYER] = fit->data;
	fit->pdata[BLAYER] = fit->data;
#ifdef _OPENMP
#pragma omp parallel for num_threads(com.max_thread) private(i) schedule(static)
#endif
	for (i = 0; i < nbdata; i++)
		data[i] = buf[i];
	fit->bitpix = fit->orig_bitpix = SHORT_IMG;
	fit->naxes[0] = fit->rx;
	fit->naxes[1] = fit->ry;
	fit->naxes[2] = 1;
	fit->naxis = 2;
	return 1;
}

static int pictofitrgb(WORD *buf, fits *fit) {
	int i, nbdata;
	WORD *data[3], *olddata = fit->data;

	nbdata = fit->rx * fit->ry;
	if ((fit->data = realloc(fit->data, nbdata * 3 * sizeof(WORD))) == NULL) {
		PRINT_ALLOC_ERR;
		if (olddata)
			free(olddata);
		return -1;
	}
	data[RLAYER] = fit->pdata[RLAYER] = fit->data;
	data[GLAYER] = fit->pdata[GLAYER] = fit->data + nbdata;
	data[BLAYER] = fit->pdata[BLAYER] = fit->data + 2 * nbdata;
#ifdef _OPENMP
#pragma omp parallel for num_threads(com.max_thread) private(i) schedule(static)
#endif
	for (i = 0; i < nbdata; i++)
		data[RLAYER][i] = buf[i + (nbdata * RLAYER)];

#ifdef _OPENMP
#pragma omp parallel for num_threads(com.max_thread) private(i) schedule(static)
#endif
	for (i = 0; i < nbdata; i++)
		data[GLAYER][i] = buf[i + (nbdata * GLAYER)];

#ifdef _OPENMP
#pragma omp parallel for num_threads(com.max_thread) private(i) schedule(static)
#endif
	for (i = 0; i < nbdata; i++)
		data[BLAYER][i] = buf[i + (nbdata * BLAYER)];

	fit->bitpix = fit->orig_bitpix = SHORT_IMG;
	fit->naxis = 3;
	fit->naxes[0] = fit->rx;
	fit->naxes[1] = fit->ry;
	fit->naxes[2] = 3;
	return 3;
}

static int _pic_read_header(struct pic_struct *pic_file) {
	char header[290];
	if (!pic_file || !pic_file->file)
		return -1;
	if (sizeof(header) != fread(header, 1, sizeof(header), pic_file->file)) {
		perror("read");
		return -1;
	}

	memcpy(&pic_file->magic, header, 4);

	if (pic_file->magic != 0x12231fc) {
		siril_log_message(_("Wrong magic cookie in PIC file. "
				"This image is not supported.\n"));
		return -1;
	}

	memcpy(&pic_file->width, header + 68, 2);
	memcpy(&pic_file->height, header + 70, 2);
	assert(pic_file->width > 0 && pic_file->height > 0);
	memcpy(pic_file->bin, header + 80, 12);
	memcpy(&pic_file->nbplane, header + 92, 2);
	assert(pic_file->nbplane != 0);
	memcpy(&pic_file->hi, header + 118, 2);
	memcpy(&pic_file->lo, header + 120, 2);
	pic_file->date = g_strndup(header + 94, 10);
	pic_file->time = g_strndup(header + 104, 12);
	return 0;
}

static int _pic_close_file(struct pic_struct *pic_file) {
	int retval = 0;
	if (!pic_file)
		return retval;
	if (pic_file->file) {
		retval = fclose(pic_file->file);
		pic_file->file = NULL;
	}
	g_free(pic_file->date);
	g_free(pic_file->time);
	free(pic_file);
	return retval;
}

int readpic(const char *name, fits *fit) {
	struct pic_struct *pic_file;
	WORD *buf;
	int retval = 0;
	unsigned int nbdata;

	pic_file = calloc(1, sizeof(struct pic_struct));

	if ((pic_file->file = g_fopen(name, "rb")) == NULL) {
		siril_log_message(
				_("Sorry but Siril cannot open the PIC file: %s.\n"), name);
		free(pic_file);
		return -1;
	}

	if (_pic_read_header(pic_file)) {
		_pic_close_file(pic_file);
		return -1;
	}

	fit->rx = (unsigned int) pic_file->width;
	fit->ry = (unsigned int) pic_file->height;
	fit->binning_x = (unsigned int) pic_file->bin[4];
	fit->binning_y = (unsigned int) pic_file->bin[5];
	fit->hi = pic_file->hi;
	fit->lo = pic_file->lo;

	nbdata = fit->rx * fit->ry;

	fseek(pic_file->file, 290, SEEK_SET);
	buf = malloc(nbdata * pic_file->nbplane * sizeof(WORD));

	if ((fread(buf, 1, nbdata * pic_file->nbplane * sizeof(WORD), pic_file->file))
			!= nbdata * pic_file->nbplane * sizeof(WORD)) {
		siril_log_message(_("Cannot Read the data\n"));
		free(buf);
		_pic_close_file(pic_file);
		return -1;
	}

	switch (pic_file->nbplane) {
	case 1:
		retval = pictofit(buf, fit);
		break;
	case 3:
		retval = pictofitrgb(buf, fit);
		break;
	default:
		retval = -1;
		siril_log_message(_("Sorry but Siril cannot open this file.\n"));
	}
	free(buf);

	if (retval) {
		_pic_close_file(pic_file);
		return -1;
	}

	char *basename = g_path_get_basename(name);
	siril_log_message(_("Reading PIC: file %s, %ld layer(s), %ux%u pixels\n"),
			basename, fit->naxes[2], fit->rx, fit->ry);
	siril_log_message("(%d,%d)-(%d,%d) - Binning %dx%d\n", pic_file->bin[0],
			pic_file->bin[1], pic_file->bin[2], pic_file->bin[3],
			fit->binning_x, fit->binning_y);

	if (pic_file->date[0] != 0x00) {
		g_strchug(pic_file->date);	// removing left white spaces if exist
		siril_log_message(_("Date (of observation): %s\n"), pic_file->date);
	}
	if (pic_file->time[0] != 0x00) {
		g_strchug(pic_file->time);	// removing left white spaces if exist
		siril_log_message(_("Time (of observation): %s\n"), pic_file->time);
	}

	_pic_close_file(pic_file);
	g_free(basename);
	return retval;
}
