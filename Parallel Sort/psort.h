// record datatype
typedef struct Records {
    int key;
    int data[24];
} Record;

// merge sort args datatype
struct mSortArgs {
    int low;
    int high;
    Record *arr;
};

// function definitions
void merge_sort(Record arr[], int left, int right);
void merge(Record arr[], int low, int middle, int high);
void* parallel_sort(void* argc);
void merge_subarr(Record arr[], int number, int aggregation);