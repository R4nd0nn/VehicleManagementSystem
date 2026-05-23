#ifndef CONTAINER_H
#define CONTAINER_H

struct Container {
    int id;
    std::string container_no;
    std::string status;
    std::string pickup_time;
    std::string return_time;
    int free_days;
    std::string abnormal_status;
    std::string abnormal_desc;
    std::string waybill_no;
    std::string port;
    std::string free_expired_time;
    std::string created_at;
    std::string updated_at;
};

#endif