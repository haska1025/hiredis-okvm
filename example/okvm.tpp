package okvm_tpp;

protocol Signlight
{
    prefix key string signlight_key='signlight';
    primary key int id;
    foreign key int fid1;
    foreign key int fid2;

    int number;
    int color;
    int size;
};

protocol Street
{
    prefix key string street_key='street';
    primary key int id;

    int number;
    string name;
    int direction;
    
    repeat string person;
    // sub-mapping
    repeat Signlight lights;
    Signlight light;
};
