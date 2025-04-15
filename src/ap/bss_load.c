/*
 * BSS Load Element / Channel Utilization
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "hostapd.h"
#include "bss_load.h"
#include "ap_drv_ops.h"
#include "beacon.h"


static int get_bss_load_update_timeout(struct hostapd_data *hapd,
				       unsigned int *sec, unsigned int *usec)
{
	unsigned int update_period = hapd->conf->bss_load_update_period;
	unsigned int beacon_int = hapd->iconf->beacon_int;
	unsigned int update_timeout;

	wpa_printf(MSG_DEBUG, "BSS Load: Update period: %u, Beacon interval: %u", update_period, beacon_int);

	if (!update_period || !beacon_int) {
		wpa_printf(MSG_ERROR,
			   "BSS Load: Invalid BSS load update configuration (period=%u beacon_int=%u)",
			   update_period, beacon_int);
		return -1;
	}

	update_timeout = update_period * beacon_int;
	wpa_printf(MSG_DEBUG, "BSS Load: Calculated update timeout: %u", update_timeout);

	*sec = ((update_timeout / 1000) * 1024) / 1000;
	*usec = (update_timeout % 1000) * 1024;

	wpa_printf(MSG_DEBUG, "BSS Load: Timeout in seconds: %u, usec: %u", *sec, *usec);

	return 0;
}


static void update_channel_utilization(void *eloop_data, void *user_data)
{
	struct hostapd_data *hapd = eloop_data;
	unsigned int sec, usec;
	int err;
	struct hostapd_iface *iface = hapd->iface;

	wpa_printf(MSG_DEBUG, "BSS Load: Entering update_channel_utilization");

	if (!(hapd->beacon_set_done && hapd->started)) {
		wpa_printf(MSG_DEBUG, "BSS Load: Beacon not set or AP not started, skipping update");
		return;
	}

	err = hostapd_drv_get_survey(hapd, hapd->iface->freq);
	if (err) {
		wpa_printf(MSG_ERROR, "BSS Load: Failed to get survey data");
		return;
	}

	wpa_printf(MSG_DEBUG, "BSS Load: Survey data retrieved successfully.");

	ieee802_11_set_beacon_per_bss_only(hapd);

	if (get_bss_load_update_timeout(hapd, &sec, &usec) < 0)
		return;

	if (hapd->conf->chan_util_avg_period) {
		wpa_printf(MSG_DEBUG, "BSS Load: Averaging channel utilization over %d periods", hapd->conf->chan_util_avg_period);
        iface->chan_util_samples_sum += iface->channel_utilization;
        iface->chan_util_num_sample_periods += hapd->conf->bss_load_update_period;

        wpa_printf(MSG_DEBUG, "BSS Load: Samples sum: %d, Sample periods: %d", iface->chan_util_samples_sum, iface->chan_util_num_sample_periods);

        if (iface->chan_util_num_sample_periods >= hapd->conf->chan_util_avg_period) {
            iface->chan_util_average =
                iface->chan_util_samples_sum /
                (iface->chan_util_num_sample_periods /
                 hapd->conf->bss_load_update_period);
            iface->chan_util_samples_sum = 0;
            iface->chan_util_num_sample_periods = 0;
            wpa_printf(MSG_INFO, "BSS AVG: %d, Channel: %d", iface->chan_util_average, iface->conf->channel);
        }
    }

	wpa_printf(MSG_DEBUG, "BSS Load: Registering next update with timeout: %u sec, %u usec", sec, usec);
	eloop_register_timeout(sec, usec, update_channel_utilization, hapd, NULL);
}


int bss_load_update_init(struct hostapd_data *hapd)
{
	unsigned int sec, usec;

	wpa_printf(MSG_DEBUG, "BSS Load: Initializing BSS load update");

	if (get_bss_load_update_timeout(hapd, &sec, &usec) < 0)
		return -1;

	wpa_printf(MSG_DEBUG, "BSS Load: Registering first update with timeout: %u sec, %u usec", sec, usec);
	eloop_register_timeout(sec, usec, update_channel_utilization, hapd, NULL);
	return 0;
}


void bss_load_update_deinit(struct hostapd_data *hapd)
{
	wpa_printf(MSG_DEBUG, "BSS Load: Deinitializing BSS load update");

	eloop_cancel_timeout(update_channel_utilization, hapd, NULL);
}

