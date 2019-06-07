#include "../include/util/common-header.h"
#include "../include/dare/dare_server.h"
#include <libconfig.h>

double hb_period;
uint64_t elec_timeout_low;
uint64_t elec_timeout_high;
double rc_info_period;
double retransmit_period;
double log_pruning_period;

int dare_read_config(const char* config_path){
    config_t config_file;
    config_init(&config_file);

    if(!config_read_file(&config_file,config_path)){
        goto goto_config_error;
    }

    config_setting_t *dare_global_config = NULL;
    dare_global_config = config_lookup(&config_file,"dare_global_config");

    if(NULL!=dare_global_config){
        double temp_float;
        if(config_setting_lookup_float(dare_global_config,"hb_period",&temp_float)){
            hb_period = temp_float;
        }
        if(config_setting_lookup_float(dare_global_config,"rc_info_period",&temp_float)){
            rc_info_period = temp_float;
        }
        if(config_setting_lookup_float(dare_global_config,"retransmit_period",&temp_float)){
            retransmit_period = temp_float;
        }
        if(config_setting_lookup_float(dare_global_config,"log_pruning_period",&temp_float)){
            log_pruning_period = temp_float;
        }
        long long temp_int64;
        if(config_setting_lookup_int64(dare_global_config,"elec_timeout_low",&temp_int64)){
            elec_timeout_low = temp_int64;
        }
        if(config_setting_lookup_int64(dare_global_config,"elec_timeout_high",&temp_int64)){
            elec_timeout_high = temp_int64;
        }
    }

    config_destroy(&config_file);
    return 0;

goto_config_error:
    err_log("%s:%d - %s\n", config_error_file(&config_file),
            config_error_line(&config_file), config_error_text(&config_file));
    config_destroy(&config_file);
    return -1;
}
