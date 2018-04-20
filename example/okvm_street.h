#ifndef _OKVM_STREET_H_
#define _OKVM_STREET_H_

#include <string>
#include <string>

class Signlight
{
public:
    Signlight(){}
    Signlight(const Signlight &sl){
        number = sl.number;
        color = sl.color;
        size = sl.size;
    }
    ~Signlight(){}
    

    int number;
    int color;
    int size;
};

class Street
{
public:
    Street(){}
    ~Street(){}

    int restore()
    {
        // Get the member
        std::ostringstream os;
        os << "hgetall " << prefix_ << ":" << id_;
        void *reply = redis_okvm_read(os.str().c_str());
        int i = 0;
        int length = 0;
        char *field = NULL;
        char *value = NULL;

        Street *s = (Street*)userdata;

        length = redis_okvm_reply_length(reply);
        for (; i < length;){
            field = redis_okvm_reply_idxof_str(reply, i++);
            value = redis_okvm_reply_idxof_str(reply, i++);
            if (strcmp(field, "name") == 0){
                s->name_ = value;
            }else if (strcmp(field, "id") == 0){
                s->id_ = value;
            }else if (strcmp(field, "number") == 0){
                s->number_ = value;
            }else if (strcmp(field, "direction") == 0){
                s->name_ = value;
            }else{
                ;//Do nothing.
            }
        }
        redis_okvm_reply_free(reply);
        reply = NULL;

        // Get sub-mapping
        std::ostringstream os2;
        os2 << "hmset signlights_rlt " << ":" << id_;
        redis_okvm_read(os.str().c_str(), os.str().size(), store_reply_cb);

        // Get the subclass
    }

    int store()
    {
        // Store the member
        std::ostringstream os;
        os << "hmset " << prefix_ << ":" << id_;
        os << " number " << number_;
        os << " name " << name_;
        os << " direction " << direction_;

        redis_okvm_write(os.str().c_str(), os.str().size());

        // Store subclass
        std::ostringstream os2;
        os2 << "sadd signlights_rlt " << ":" << id_;
        std::set<Signlight>::iterator it = lights_.begin();
        for(; it != lights_.end(); ++it){
            os2 << " " << it->get_id();
//            it->store(); 
        }

        // Store sub-mapping
        redis_okvm_write(os2.str().c_str(), os2.str().size());
    }

    int get()
    {
        // doesn't load sub-mapping
        return 0;
    }

    int remove()
    {
        gstream os;
        os << "hdel " << prefix_ << ":" << id_;
        os << " number ";
        os << " name ";
        os << " direction ";

        redis_okvm_write(os.str().c_str(), os.str().size());
    }

    int add_to_signlights_rlt(int id)
    {
        std::ostringstream os2;
        os2 << "sadd signlights_rlt " << ":" << id_;
        os2 << " " << id;
    }
    int remove_from_signlights_rlt(int id)
    {
        std::ostringstream os2;
        os2 << "srem signlights_rlt " << ":" << id_;
        os2 << " " << id;
    }

    std::string prefix_;
    // Just support one key
    int id_;

    int number_;
    std::string name_;
    int direction_;
    
    // sub-mapping
    std::set<Signlight> lights_;
};
#endif//_OKVM_STREET_H_

