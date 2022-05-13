#include "global.h"
#include "data_manager.h"
#include "data_conf.h"
#include "dtc_global.h"
#include "config.h"
#include "proc_title.h"

const char data_project_name[] = "data_lifecycle_manager";

int main(int argc, char *argv[]){
    init_proc_title(argc, argv);
    set_proc_title("agent-data-lifecycle");
    init_log4cplus();
    if(DataConf::Instance()->LoadConfig(argc, argv) != 0){
        log4cplus_error("load_config error.");
        return DTC_CODE_LOAD_CONFIG_ERR;
    }
    ConfigParam config_param;
    if(DataConf::Instance()->ParseConfig(config_param) != 0){
        log4cplus_error("parse_config error.");
        return DTC_CODE_PARSE_CONFIG_ERR;
    }
    log4cplus_info("%s v%s: starting....", data_project_name, version);
    if(init_daemon() < 0){
        log4cplus_error("init_daemon error.");
        return DTC_CODE_INIT_DAEMON_ERR;
    }
    DataManager* p_data_manager = new DataManager(config_param);
    if(0 != p_data_manager->ConnectAgent()){
        log4cplus_error("ConnectAgent error.");
        return DTC_CODE_INIT_DAEMON_ERR;
    }
    p_data_manager->DoProcess();
    if(NULL != p_data_manager){
        delete p_data_manager;
    }
    if(NULL != g_dtc_config){
        delete g_dtc_config;
    }
    log4cplus_info("%s v%s: stopped", data_project_name, version);
    log4cplus::deinitialize();
    daemon_cleanup();
    return 0;
}