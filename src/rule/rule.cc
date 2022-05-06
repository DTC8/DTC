#include "rule.h"
#include <stdio.h>
#include <iostream>
#include "../libs/hsql/include/SQLParser.h"
#include "../libs/hsql/include/util/sqlhelper.h"
#include "re_comm.h"
#include "re_load.h"
#include "re_match.h"
#include "re_cache.h"
#include "log.h"
#include "yaml-cpp/yaml.h"

using namespace std;

extern vector<vector<hsql::Expr*> > expr_rules;

extern "C" int rule_sql_match(const char* szsql, const char* szkey)
{
    if(!szsql || !szkey)
        return -1;
        
    std::string key = "";
    std::string sql = szsql;

    key = szkey;
    if(key.length() == 0)
        return -1;

    cout<<"key: "<<key<<endl;
    cout<<"sql: "<<sql<<endl;

    init_log4cplus();
    
    int ret = re_load_rule();
    if(ret != 0)
    {
        log4cplus_error("load rule error:%d", ret);
        return -5;
    }

    if(sql.find("INSERT INTO") != sql.npos || sql.find("insert into") != sql.npos)
        return 1;

    hsql::SQLParserResult sql_ast;
    if(re_parse_sql(sql, &sql_ast) != 0)
        return -1;

    ret = re_match_sql(&sql_ast, expr_rules);
    if(ret == 0)
    {
        if(re_is_cache_sql(&sql_ast, key))
        {
            //L1: DTC cache.
            return 1;
        }
        else
        {
            //L2: sharding hot database.
            return 2;
        }
    }
    else {
        //L3: full database.
        return 3;
    }

    return 3;
}

