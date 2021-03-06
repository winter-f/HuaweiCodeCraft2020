#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sched.h>
#include <pthread.h>
#include <cstring>
#include <sys/mman.h>
#include <arm_neon.h>
using namespace std;

// #define DEBUG

#ifdef DEBUG
#include <cstdio>
#include <ctime>
#include <stdarg.h>
int _dbg(const char *format, ...) {
    va_list argPtr;
    int cnt = 0;
 
    va_start(argPtr, format);
    fflush(stdout);
    cnt = vfprintf(stderr, format, argPtr);
    va_end(argPtr);
    return cnt;
}
#else
inline void _dbg(const char *format, ...) {}
#endif

const int N = 1600;
const int M = 1000;
const int _N = 20000;
const int THREAD_NUM = 4;
const int BATCH_SIZE = _N / THREAD_NUM;

char Y_test[_N];
int32_t read_result[M], mean[2][M], cnt[2];

inline void Train(const char* filename) {
    #ifdef DEBUG
    clock_t start = clock();
    #endif

    uint8_t label;

    int fd = open(filename, O_RDONLY);
    struct stat fs;
    fstat(fd, &fs);

    uint8_t *p = (uint8_t*)mmap(NULL, fs.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    for(int i = 0; i < N; ++i) {
        for(int j = 0; j < M; ++j){
            while(*p ^ '.')++p;
            // read_result[j] = ((*(p+1) ^ 48) << 3 ) + ((*(p+1) ^ 48) << 1 ) + (*(p+2) ^ 48);
            read_result[j] = (int32_t)(*(p+1)) << 5;
            p = p + 5;
        }
        label = *(p) ^ 48;
        ++cnt[label];
        for(int j = 0; j < M; j += 4) {
            int32x4_t a = vld1q_s32(mean[label] + j);
            int32x4_t b = vld1q_s32(read_result + j);
            a = vaddq_s32(a, b);
            vst1q_s32(mean[label] + j, a);
        }
    }

    for(int j = 0; j < M; ++j) mean[0][j] /= cnt[0];
    for(int j = 0; j < M; ++j) mean[1][j] /= cnt[1];

    #ifdef DEBUG
    clock_t end = clock();
    _dbg("Train (%d, %d) success, cost %.5f s\n", N, M, (float)(end - start) / CLOCKS_PER_SEC);
    #endif
}

struct ThreadInfo {
    uint8_t *p;
    char *y;
    ThreadInfo(){}
    ThreadInfo(uint8_t* _p, char* _y)
        :y(_y), p(_p)
        {}
} __attribute__((aligned (64)));

void* predict(void* _info) {
    ThreadInfo *info = (ThreadInfo*)(_info);
    for(int i = 0; i < BATCH_SIZE; ++i) {
        int32_t dist0 = 0, dist1 = 0;

        for(int j = 0; j < M ; ++j){
            int32_t x = (int32_t)(*(info->p+2)) << 5;
            info->p += 6;
            dist0 += (x - mean[0][j]) * (x - mean[0][j]);
            dist1 += (x - mean[1][j]) * (x - mean[1][j]);
        }

        info->y[i] = (dist0 < dist1 ? '0' : '1');
    }
} 




inline void Predict(const char* filename) {
    #ifdef DEBUG
    clock_t start = clock();
    #endif

    int fd = open(filename, O_RDONLY);

    struct stat fs;
    fstat(fd, &fs);

    uint8_t *p = (uint8_t*)mmap(NULL, fs.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    ThreadInfo infos[THREAD_NUM];
    pthread_t threads[THREAD_NUM];
    for(int i = 0; i < THREAD_NUM; ++i) {
        infos[i] = ThreadInfo(p + i * 6000 * BATCH_SIZE, Y_test + i * BATCH_SIZE);
        pthread_create(&threads[i], NULL, predict, (void*)(infos + i));
    }

    for(int i = 0; i < THREAD_NUM; ++i) {
        pthread_join(threads[i], NULL);
    }

    #ifdef DEBUG
    clock_t end = clock();
    _dbg("Predict (%d, %d) success, cost %.5f s\n", _N, M, (float)(end - start) / CLOCKS_PER_SEC);
    #endif
}

bool loadAnswerData(const char* awFile, vector<int>& awVec) {
    ifstream infile(awFile);
    if (!infile) {
        _dbg("Open file error\n");
        exit(0);
    }

    while (infile) {
        string line;
        int aw;
        getline(infile, line);
        if (line.size() > 0) {
            stringstream sin(line);
            sin >> aw;
            awVec.push_back(aw);
        }
    }

    infile.close();
    return true;
}

// save predict result
inline void savePredictResult(const char* filename) {
    #ifdef DEBUG
    _dbg("Saving predict result\n");
    clock_t start = clock();
    #endif
    FILE *output = fopen(filename, "w");
    if(output == NULL) {
        _dbg("Open predictFile Error\n");
        return;
    }

    for(int i=0; i < _N; ++i) {
        fputc(Y_test[i], output);
        fputc('\n', output);
    }

    fclose(output);

    #ifdef DEBUG
    clock_t end = clock();
    _dbg("Save predict result success, cost %.5f s\n", (float)(end - start) / CLOCKS_PER_SEC);
    #endif
}

int main(int argc, char* argv[])
{
#ifdef DEBUG
    const char* trainfile = "./data/train_data.txt";
    const char* testfile = "./data/test_data.txt";
    const char* predictfile = "./projects/student/result.txt";
    const char* answerfile = "./projects/student/answer.txt";
    clock_t start = clock();
#else
    const char* trainfile = "/data/train_data.txt";
    const char* testfile = "/data/test_data.txt";
    const char* predictfile = "/projects/student/result.txt";
    const char* answerfile = "/projects/student/answer.txt";
#endif

    Train(trainfile);

    Predict(testfile);

    savePredictResult(predictfile);

#ifdef DEBUG
    clock_t end = clock();
    vector<int> answerVec, predictVec;
    _dbg("loading answer\n");
    loadAnswerData(answerfile, answerVec);

    _dbg("loading predict\n");
    loadAnswerData(predictfile, predictVec);

    _dbg("answer size : %d\n", (int)answerVec.size());
    _dbg("predict size : %d\n", (int)predictVec.size());

    int correctCount = 0;
    float accuracy;
    for (int j = 0; j < (int)predictVec.size(); ++j)
        if (answerVec[j] == predictVec[j])
            correctCount++;

    accuracy = ((float)correctCount) / answerVec.size();
    _dbg("accuracy : %.5f\n", accuracy);
    _dbg("total time : %.5f s\n", (float)(end - start) / CLOCKS_PER_SEC);
#endif

    return 0;
}