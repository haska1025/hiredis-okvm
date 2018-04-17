package okvm_tpp;

protocol Signlight
{
    prefix key string prefix;
    primary key int id;
    foreign key int fid1;
    foreign key int fid2;

    int number;
    int color;
    int size;
};

protocol Street
{
    prefix key string prefix;
    primary key int id;

    int number;
    string name;
    int direction;
    
    // sub-mapping
    set Signlight lights;
    set prefix key string signlights_rlt;
    set key int id;
};
