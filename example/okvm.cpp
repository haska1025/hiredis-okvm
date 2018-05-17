#include <stdio.h>
#include <hiredis_okvm.h>
#include <hiredis_okvm_log.h>

#include "okvm.tpp.h"

void test_set_signlight()
{
    Signlight sl1;

    sl1.set_fid1(21);
    sl1.set_fid2(22);
    sl1.set_id(11, true);
    sl1.set_fid1(21, true);
    sl1.set_fid2(22, true);
    sl1.set_number(83753, true);
    sl1.set_color(1, true);
    sl1.set_size(2, true);

    sl1.dump(std::cout);
}

void test_load_signlight()
{
    Signlight sl1;

    sl1.set_fid1(21);
    sl1.set_fid2(22);
    sl1.set_id(11);

    sl1.Load();

    sl1.dump(std::cout);
}

void test_set_street()
{
    Street s;

    s.set_id(1100, true);
    s.set_number(12345, true);
    s.set_name("science&technique8", true);
    s.set_direction(1, true);

    std::vector<std::string> persons;
    persons.push_back("zhangsan");
    persons.push_back("lisi");
    persons.push_back("wanger");
    s.add_person(persons, true);

    std::vector<Signlight> sls;
    Signlight sl;
    sl.set_id(11);
    sls.push_back(sl);
    sl.set_id(12);
    sls.push_back(sl);

    s.add_lights(sls, true);

    sl.set_id(88);
    s.set_light(sl, true);

    s.dump(std::cout);
}

void test_load_street()
{
    Street s;

    s.set_id(1100);
    s.Load();

    std::cout << "===============load streat=================" << std::endl;
    s.dump(std::cout);
}

int main(int argc, char *argv[])
{
    char *host = NULL; 
    int rc = 0;
    struct redis_okvm okvm;

    INIT_HIREDIS_OKVM(&okvm);

    okvm.read_connections = 1;
    okvm.write_connections = 1;
    okvm.db_index = 2;
    okvm.redis_host = "192.168.32.84:26379;192.168.32.107:26379;192.168.32.104:26379";
    //okvm.redis_host = "127.0.0.1:26379;127.0.0.1:26380;127.0.0.1:26381";
    okvm.db_name = "tang-cluster";
    okvm.password = "d8e8fca2dc0f896fd7cb4cb0031ba249";
    //            //okvm.db_name = "mymaster";

    rc = redis_okvm_init(&okvm);
    if (rc != 0){
        HIREDIS_OKVM_LOG_ERROR("Init okvm failed");
        redis_okvm_fini();
        return -1;
    }

    test_set_signlight();
    test_load_signlight();
    test_set_street();
    test_load_street();

    return 0;
}

