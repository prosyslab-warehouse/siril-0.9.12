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

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#include <gsl/gsl_matrix.h>

#include "core/siril.h"
#include "core/proto.h"
#include "algos/Def_Wavelet.h"
#include "gui/callbacks.h"
#include "gui/message_dialog.h"
#include "gui/progress_and_log.h"
#include "algos/PSF.h"
#include "gui/PSF_list.h"
#include "algos/star_finder.h"
#include "algos/statistics.h"

#define WAVELET_SCALE 3

static WORD Compute_threshold(fits *fit, double ksigma, int layer, WORD *norm, double *bg) {
	WORD threshold;
	imstats *stat;

	assert(layer <= 3);

	stat = statistics(NULL, -1, fit, layer, NULL, STATS_BASIC);
	if (!stat) {
		siril_log_message(_("Error: statistics computation failed.\n"));
		*norm = 0;
		*bg = 0.0;
		return 0;
	}
	threshold = round_to_WORD(stat->median + ksigma * stat->sigma);
	*norm = (WORD)stat->normValue;
	*bg = stat->median;
	free_stats(stat);

	return threshold;
}

static gboolean is_star(fitted_PSF *result, star_finder_params *sf) {
	if (isnan(result->fwhmx) || isnan(result->fwhmy))
		return FALSE;
	if (isnan(result->x0) || isnan(result->y0))
		return FALSE;
	if (isnan(result->mag))
		return FALSE;
	if ((result->x0 <= 0.0) || (result->y0 <= 0.0))
		return FALSE;
	if (result->A < 0.01)
		return FALSE;
	if (result->sx > 200 || result->sy > 200)
		return FALSE;
	if (result->fwhmx <= 0.0 || result->fwhmy <= 0.0)
		return FALSE;
	if ((result->fwhmy / result->fwhmx) < sf->roundness)
		return FALSE;

	return TRUE;
}

static void get_structure(star_finder_params *sf) {
	static GtkSpinButton *spin_radius = NULL, *spin_sigma = NULL,
			*spin_roundness = NULL;

	if (spin_radius == NULL) {
		spin_radius = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_radius"));
		spin_sigma = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_threshold"));
		spin_roundness = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_round"));
	}
	sf->radius = (int) gtk_spin_button_get_value(spin_radius);
	sf->sigma = gtk_spin_button_get_value(spin_sigma);
	sf->roundness = gtk_spin_button_get_value(spin_roundness);
}

void init_peaker_GUI() {
	/* TODO someday: read values from conf file and set them in the GUI.
	 * Until then, storing values in com.starfinder_conf instead of getting
	 * them in the GUI while running the peaker.
	 * see also init_peaker_default below */
	get_structure(&com.starfinder_conf);
}

void init_peaker_default() {
	/* values taken from siril3.glade */
	com.starfinder_conf.radius = 10;
	com.starfinder_conf.sigma = 1.0;
	com.starfinder_conf.roundness = 0.5;
}

void on_spin_sf_radius_changed(GtkSpinButton *spinbutton, gpointer user_data) {
	com.starfinder_conf.radius = (int)gtk_spin_button_get_value(spinbutton);
}

void on_spin_sf_threshold_changed(GtkSpinButton *spinbutton, gpointer user_data) {
	com.starfinder_conf.sigma = gtk_spin_button_get_value(spinbutton);
}

void on_spin_sf_roundness_changed(GtkSpinButton *spinbutton, gpointer user_data) {
	com.starfinder_conf.roundness = gtk_spin_button_get_value(spinbutton);
}

void update_peaker_GUI() {
	static GtkSpinButton *spin_radius = NULL, *spin_sigma = NULL,
			*spin_roundness = NULL;

	if (spin_radius == NULL) {
		spin_radius = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_radius"));
		spin_sigma = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_threshold"));
		spin_roundness = GTK_SPIN_BUTTON(lookup_widget("spinstarfinder_round"));
	}
	gtk_spin_button_set_value(spin_radius, (double) com.starfinder_conf.radius);
	gtk_spin_button_set_value(spin_sigma, com.starfinder_conf.sigma);
	gtk_spin_button_set_value(spin_roundness, com.starfinder_conf.roundness);
}

/*
 This is an implementation of a simple peak detector algorithm which
 identifies any pixel that is greater than any of its eight neighbors.

 Original algorithm come from:
 Copyleft (L) 1998 Kenneth J. Mighell (Kitt Peak National Observatory)
 */

/* returns a NULL-ended array of FWHM info */
fitted_PSF **peaker(fits *fit, int layer, star_finder_params *sf, int *nb_stars, rectangle *area, gboolean showtime) {
	int nx = fit->rx;
	int ny = fit->ry;
	int areaX0 = 0;
	int areaY0 = 0;
	int areaX1 = nx;
	int areaY1 = ny;
	int y, k, nbstars = 0;
	double bg;
	WORD threshold, norm;
	WORD **wave_image, **real_image;
	fits wave_fit = { 0 };
	fitted_PSF **results;
	struct timeval t_start, t_end;

	assert(nx > 0 && ny > 0);

	results = malloc((MAX_STARS + 1) * sizeof(fitted_PSF *));
	if (!results) {
		PRINT_ALLOC_ERR;
		return NULL;
	}

	siril_log_color_message(_("Findstar: processing...\n"), "red");
	gettimeofday(&t_start, NULL);

	results[0] = NULL;
	threshold = Compute_threshold(fit, sf->sigma, layer, &norm, &bg);
	if (norm == 0) {
		free(results);
		return NULL;
	}

	copyfits(fit, &wave_fit, CP_ALLOC | CP_FORMAT | CP_COPYA, 0);
	get_wavelet_layers(&wave_fit, WAVELET_SCALE, 2, TO_PAVE_BSPLINE, layer);

	/* FILL wavelet image upside-down */
	wave_image = malloc(ny * sizeof(WORD *));
	if (wave_image == NULL) {
		free(results);
		clearfits(&wave_fit);
		PRINT_ALLOC_ERR;
		return NULL;
	}
	for (k = 0; k < ny; k++)
		wave_image[ny - k - 1] = wave_fit.pdata[layer] + k * nx;

	/* FILL real image upside-down */
	real_image = malloc(ny * sizeof(WORD *));
	if (real_image == NULL) {
		free(results);
		free(wave_image);
		clearfits(&wave_fit);
		PRINT_ALLOC_ERR;
		return NULL;
	}
	for (k = 0; k < ny; k++)
		real_image[ny - k - 1] = fit->pdata[layer] + k * nx;

	if (area) {
		areaX0 = area->x;
		areaY0 = area->y;
		areaX1 = area->w + areaX0;
		areaY1 = area->h + areaY0;
	}

	for (y = sf->radius + areaY0; y < areaY1 - sf->radius; y++) {
		int x;
		for (x = sf->radius + areaX0; x < areaX1 - sf->radius; x++) {
			WORD pixel = wave_image[y][x];
			if (pixel > threshold && pixel < norm) {
				int yy, xx;
				gboolean bingo = TRUE;
				WORD neighbor;
				for (yy = y - 1; yy <= y + 1; yy++) {
					for (xx = x - 1; xx <= x + 1; xx++) {
						if (xx == x && yy == y)
							continue;
						neighbor = wave_image[yy][xx];
						if (neighbor > pixel) {
							bingo = FALSE;
							break;
						} else if (neighbor == pixel) {
							if ((xx <= x && yy <= y) || (xx > x && yy < y)) {
								bingo = FALSE;
								break;
							}
						}
					}
				}
				if (bingo && nbstars < MAX_STARS) {
					int ii, jj, i, j;
					//~ fprintf(stdout, "Found a probable star at position (%d, %d) with a value of %hu\n", x, y, pixel);
					gsl_matrix *z = gsl_matrix_alloc(sf->radius * 2, sf->radius * 2);
					/* FILL z */
					for (jj = 0, j = y - sf->radius; j < y + sf->radius;
							j++, jj++) {
						for (ii = 0, i = x - sf->radius; i < x + sf->radius;
								i++, ii++) {
							gsl_matrix_set(z, ii, jj, (double)real_image[j][i]);
						}
					}
					/* ****** */
					/* In this case the angle is not fitted because it
					 *  slows down the algorithm too much 
					 * To fit the angle, set the 4th parameter to TRUE */
					fitted_PSF *cur_star = psf_global_minimisation(z, bg, layer,
							FALSE, FALSE, FALSE);
					if (cur_star) {
						fwhm_to_arcsec_if_needed(fit, &cur_star);
						if (is_star(cur_star, sf)) {
							cur_star->xpos = x + cur_star->x0 - sf->radius - 1.0;
							cur_star->ypos = y + cur_star->y0 - sf->radius - 1.0;
							if (nbstars < MAX_STARS) {
								results[nbstars] = cur_star;
								results[nbstars + 1] = NULL;
//								printf("%f\t\t%f\t\t%f\n", cur_star->xpos, cur_star->ypos, cur_star->mag);
								nbstars++;
							}

						}
					}
					gsl_matrix_free(z);
				}
			}
		}
	}

	if (nbstars == 0) {
		free(results);
		results = NULL;
	}
	sort_stars(results, nbstars);
	free(wave_image);
	free(real_image);
	clearfits(&wave_fit);

	gettimeofday(&t_end, NULL);
	if (showtime)
		show_time(t_start, t_end);
	if (nb_stars)
		*nb_stars = nbstars;
	return results;
}

/* Function to add star one by one, from the selection rectangle, the
 * minimization is run and the star is detected and added to the list of stars.
 *
 * IF A STAR IS FOUND and not already present in com.stars, the return value is
 * the new star and index is set to the index of the new star in com.stars.
 * IF NO NEW STAR WAS FOUND, either because it was already in the list, or a
 * star failed to be detected in the selection, or any other error, the return
 * value is NULL and index is set to -1.
 */
fitted_PSF *add_star(fits *fit, int layer, int *index) {
	int i = 0;
	gboolean already_found = FALSE;

	*index = -1;
	fitted_PSF * result = psf_get_minimisation(&gfit, layer, &com.selection, FALSE, TRUE);
	if (!result)
		return NULL;
	/* We do not check if it's matching with the "is_star()" criteria.
	 * Indeed, in this case the user can add manually stars missed by star_finder */

	if (com.stars) {
		// check if the star was already detected/peaked
		while (com.stars[i]) {
			if (fabs(result->x0 + com.selection.x - com.stars[i]->xpos) < 0.9
					&& fabs(
							com.selection.y + com.selection.h - result->y0
									- com.stars[i]->ypos) < 0.9)
				already_found = TRUE;
			i++;
		}
	} else {
		com.stars = malloc((MAX_STARS + 1) * sizeof(fitted_PSF*));
		if (!com.stars) {
			PRINT_ALLOC_ERR;
			return NULL;
		}
		com.star_is_seqdata = FALSE;
	}

	if (already_found) {
		free(result);
		result = NULL;
		char *msg = siril_log_message(_("This star has already been picked !\n"));
		siril_message_dialog( GTK_MESSAGE_INFO, _("Peaker"), msg);
	} else {
		if (i < MAX_STARS) {
			result->xpos = result->x0 + com.selection.x;
			result->ypos = com.selection.y + com.selection.h - result->y0;
			com.stars[i] = result;
			com.stars[i + 1] = NULL;
			*index = i;
		} else {
			free(result);
			result = NULL;
		}
	}
	return result;
}

int get_size_star_tab() {
	int i = 0;

	while (com.stars[i])
		i++;
	return i;
}

/* Remove a star from com.stars, at index index. The star is freed. */
int remove_star(int index) {
	if (index < 0 || !com.stars || !com.stars[index])
		return 1;

	int N = get_size_star_tab() + 1;

	free(com.stars[index]);
	memmove(&com.stars[index], &com.stars[index + 1],
			(N - index - 1) * sizeof(*com.stars));
	redraw(com.cvport, REMAP_NONE);
	return 0;
}

int compare_stars(const void* star1, const void* star2) {
	fitted_PSF *s1 = *(fitted_PSF**) star1;
	fitted_PSF *s2 = *(fitted_PSF**) star2;

	if (s1->mag < s2->mag)
		return -1;
	else if (s1->mag > s2->mag)
		return 1;
	else
		return 0;
}

void sort_stars(fitted_PSF **stars, int total) {
	if (*(&stars))
		qsort(*(&stars), total, sizeof(fitted_PSF*), compare_stars);
}

void free_fitted_stars(fitted_PSF **stars) {
	int i = 0;
	while (stars && stars[i])
		free(stars[i++]);
	free(stars);
}

void FWHM_average(fitted_PSF **stars, int nb, float *FWHMx, float *FWHMy, char **units) {
	if (stars && stars[0]) {
		int i = 0;

		*FWHMx = 0.0f;
		*FWHMy = 0.0f;
		*units = stars[0]->units;
		while (i < nb) {
			*FWHMx += (float) stars[i]->fwhmx;
			*FWHMy += (float) stars[i]->fwhmy;
			i++;
		}
		*FWHMx = *FWHMx / (float)i;
		*FWHMy = *FWHMy / (float)i;
	}
}
