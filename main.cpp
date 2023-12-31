/*************************************************************************/
/*                                                                       */
/*  Copyright (c) 1994 Stanford University                               */
/*                                                                       */
/*  All rights reserved.                                                 */
/*                                                                       */
/*  Permission is given to use, copy, and modify this software for any   */
/*  non-commercial purpose as long as this copyright notice is not       */
/*  removed.  All other uses, including redistribution in whole or in    */
/*  part, are forbidden without prior written permission.                */
/*                                                                       */
/*  This software is provided with absolutely no warranty and no         */
/*  support.                                                             */
/*                                                                       */
/*************************************************************************/

/*************************************************************************/
/*                                                                       */
/*  Parallel dense blocked LU factorization (no pivoting)                */
/*                                                                       */
/*  This version contains one dimensional arrays in which the matrix     */
/*  to be factored is stored.                                            */
/*                                                                       */
/*  Command line options:                                                */
/*                                                                       */
/*  -nN : Decompose NxN matrix.                                          */
/*  -pP : P = number of processors.                                      */
/*  -bB : Use a block size of B. BxB elements should fit in cache for    */
/*        good performance. Small block sizes (B=8, B=16) work well.     */
/*  -s  : Print individual processor timing statistics.                  */
/*  -t  : Test output.                                                   */
/*  -o  : Print out matrix values.                                       */
/*  -h  : Print out command line options.                                */
/*                                                                       */
/*  Note: This version works under both the FORK and SPROC models        */
/*                                                                       */
/*************************************************************************/

// https://pages.cs.wisc.edu/~markhill/restricted/isca95_splash2.pdf -> I SHOULD READ THIS.
/*

LU: The LU kernel factors a dense matrix into the product of a lower
triangular and an upper triangular matrix. The dense n x n matrix A
is divided into an N x N array of B x B blocks (n = NB) to exploit
temporal locality on submatrix elements. To reduce communication,
block ownership is assigned using a 2-D scatter decomposition, with
blocks being updated by the processors that own them. The block
size B should be large enough to keep the cache miss rate low, and
small enough to maintain good load balance. Fairly small block sizes
(B=8 or B=l 6) strike a good balance in practice. Elements within a
block are allocated contiguously to improve spatial locality benefits,
and blocks are allocated locally to processors that own them.
See [WSH94] for more details.

*/

/*

CHA Optimization

Written shared data is A matrix. I will mark the lines that this specific matrix is written to.

*/


#include <algorithm>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>


#include <pthread.h>
#include <stdlib.h>
#include <semaphore.h>
#include <assert.h>
#if __STDC_VERSION__ >= 201112L
#include <stdatomic.h>
#endif
#include <stdint.h>
#define PAGE_SIZE 4096
#define __MAX_THREADS__ 256

const int CACHELINE_SIZE = 64;

#include <mutex>
#include <map>
#include <set>
#include <iostream>
#include <vector>
#include <chrono>

#include "cha.hpp"
#include "topology.hpp"
// AYDIN
std::mutex map_mutex;
std::map<long, std::multiset<double *>> // TODO: set the address type accordingly.
    threadid_addresses_map;  // will use std::set_intersection. that is why I used a set. I picked multiset instead of
                             // set since if a thread pair communicates over same addresses multiple times, I want to
                             // take this into account.

int getMostAccessedCHA(int tid1,
                       int tid2,
                       std::multiset<std::tuple<int, int, int, int>, std::greater<>> ranked_cha_access_count_per_pair,
                       Topology topo)
{
    int max = 0;
    std::vector<int> considered_chas;
    std::map<int, bool> considered_chas_flag;

    auto it = ranked_cha_access_count_per_pair.begin();
    while (it != ranked_cha_access_count_per_pair.end())
    {
        // std::pair<int, int> tid_pair(std::get<2>(*it), std::get<3>(*it));
        if ((std::get<2>(*it) == tid1 && std::get<3>(*it) == tid2) || (std::get<3>(*it) == tid1 && std::get<2>(*it) == tid2))
        {
            // SPDLOG_INFO("returning {}", std::get<1>(*it));
            max = std::get<0>(*it);
            considered_chas_flag[std::get<1>(*it)] = true;
            considered_chas.push_back(std::get<1>(*it));
            break;
        }
        it++;
    }

    // SPDLOG_INFO("communication between threads {} and {} uses the following chas the most", std::get<1>(*it), std::get<0>(*it), max);
    while (it != ranked_cha_access_count_per_pair.end())
    {
        // std::pair<int, int> tid_pair(std::get<2>(*it), std::get<3>(*it));
        if ((std::get<2>(*it) == tid1 && std::get<3>(*it) == tid2) || (std::get<3>(*it) == tid1 && std::get<2>(*it) == tid2))
        {
            // SPDLOG_INFO("returning {}", std::get<1>(*it));
            if (considered_chas_flag[std::get<1>(*it)] == false && std::get<0>(*it) > (0.9 * max))
            {
                // SPDLOG_INFO("cha {}, access count: {}, max: {}", std::get<1>(*it), std::get<0>(*it), max);
                considered_chas_flag[std::get<1>(*it)] = true;
                considered_chas.push_back(std::get<1>(*it));
            }
        }
        it++;
    }

    int x_total = 0;
    int y_total = 0;
    int cha_count = 0;
    // SPDLOG_INFO("communication between threads {} and {} involves the following CHAs:");
    for (auto it1 : considered_chas)
    {
        auto tile = topo.getTile(it1);
        x_total += tile.x;
        y_total += tile.y;
        cha_count++;
        // SPDLOG_INFO("cha {}, x: {}, y: {}", tile.cha, tile.x, tile.y);
    }

    assert(cha_count != 0);
    int x_coord = x_total / cha_count;
    int y_coord = y_total / cha_count;
    auto tile = topo.getTile(x_coord, y_coord);
    // SPDLOG_INFO("the center of gravity is cha {}, x: {}, y: {}", tile.cha, tile.x, tile.y);
    // approximate the algorithm now
    return tile.cha;
}

void stick_this_thread_to_core(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores) {
        std::cerr << "error binding thread to core: " << core_id << '\n';
        // SPDLOG_ERROR("error binding thread to core {}!", core_id);
        return;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();

    int res = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

    if (res == 0) {
        // std::cout << "thread bound to core " << core_id << std::endl;
        //        SPDLOG_INFO("Thread bound to core {} successfully.", core_id);
    } else {
        //        SPDLOG_ERROR("Error in binding this thread to core {}.", core_id);
    }
}

void assertRoot() {
    uid_t uid = getuid();
    if (uid == 0) {
        std::cout << "Running as root." << std::endl;
    } else {
        std::cerr << "Not running as root. Need root privileges to run the app. Exiting.\n";
        exit(EXIT_FAILURE);
    }
}


pthread_t __tid__[__MAX_THREADS__];
unsigned __threads__=0;
pthread_mutex_t __intern__;


#define MAXRAND					32767.0
#define DEFAULT_N				128
#define DEFAULT_P				1
#define DEFAULT_B				16
#define min(a,b) ((a) < (b) ? (a) : (b))
#define PAGE_SIZE				4096

struct GlobalMemory {
  double *t_in_fac;
  double *t_in_solve;
  double *t_in_mod;
  double *t_in_bar;
  double *completion;
  unsigned long starttime;
  unsigned long rf;
  unsigned long rs;
  unsigned long done;
  long id;
  struct { pthread_mutex_t bar_mutex; pthread_cond_t bar_cond; unsigned bar_teller; } start;
  pthread_mutex_t idlock;
} *Global;

struct LocalCopies {
  double t_in_fac;
  double t_in_solve;
  double t_in_mod;
  double t_in_bar;
};

long n = DEFAULT_N;          /* The size of the matrix */
long P = DEFAULT_P;          /* Number of processors */
long block_size = DEFAULT_B; /* Block dimension */
long nblocks;                /* Number of blocks in each dimension */
long num_rows;               /* Number of processors per row of processor grid */
long num_cols;               /* Number of processors per col of processor grid */
double *a;                   /* a = lu; l and u both placed back in a */
double *rhs;
long *proc_bytes;            /* Bytes to malloc per processor to hold blocks of A*/
long test_result = 0;        /* Test result of factorization? */
long doprint = 0;            /* Print out matrix values? */
long dostats = 0;            /* Print out individual processor statistics? */

void* SlaveStart(void*);
void OneSolve(long n, long block_size, long MyNum, long dostats);
void lu0(double *a, long n, long stride, long MyNum);
void bdiv(double *a, double *diag, long stride_a, long stride_diag, long dimi, long dimk, long MyNum);
void bmodd(double *a, double *c, long dimi, long dimj, long stride_a, long stride_c, long MyNum);
void bmod(double *a, double *b, double *c, long dimi, long dimj, long dimk, long stride, long MyNum);
void daxpy(double *a, double *b, long n, double alpha, long MyNum);
long BlockOwner(long I, long J);
long BlockOwnerColumn(long I, long J);
long BlockOwnerRow(long I, long J);
void lu(long n, long bs, long MyNum, struct LocalCopies *lc, long dostats);
void InitA(double *rhs);
double TouchA(long bs, long MyNum);
void PrintA(void);
void CheckResult(long n, double *a, double *rhs);
void printerr(const char *s);



int findCha(const double* val)
{
  // this part is changed wrt fluidanimate.
    return findCHAByHashing(reinterpret_cast<uintptr_t>(val)); // AYDIN: this is not &val, right?
}

int main(int argc, char *argv[])
{
    using namespace std;
    using std::chrono::duration;
    using std::chrono::duration_cast;
    using std::chrono::high_resolution_clock;
    using std::chrono::milliseconds;

  long i, ch;
  extern char *optarg;
  double mint, maxt, avgt;
  double min_fac, min_solve, min_mod, min_bar;
  double max_fac, max_solve, max_mod, max_bar;
  double avg_fac, avg_solve, avg_mod, avg_bar;
  unsigned long start;

  {long time{}; (start) = ::time(0);};

  while ((ch = getopt(argc, argv, "n:p:b:cstoh")) != -1) {
    switch(ch) {
    case 'n': n = atoi(optarg); break;
    case 'p': P = atoi(optarg); break;
    case 'b': block_size = atoi(optarg); break;
    case 's': dostats = 1; break;
    case 't': test_result = !test_result; break;
    case 'o': doprint = !doprint; break;
    case 'h': printf("Usage: LU <options>\n\n");
              printf("options:\n");
              printf("  -nN : Decompose NxN matrix.\n");
              printf("  -pP : P = number of processors.\n");
              printf("  -bB : Use a block size of B. BxB elements should fit in cache for \n");
              printf("        good performance. Small block sizes (B=8, B=16) work well.\n");
              printf("  -c  : Copy non-locally allocated blocks to local memory before use.\n");
              printf("  -s  : Print individual processor timing statistics.\n");
              printf("  -t  : Test output.\n");
              printf("  -o  : Print out matrix values.\n");
              printf("  -h  : Print out command line options.\n\n");
              printf("Default: LU -n%1d -p%1d -b%1d\n",
                     DEFAULT_N,DEFAULT_P,DEFAULT_B);
              exit(0);
              break;
    }
  }

  {__tid__[__threads__++]=pthread_self();}

  printf("\n");
  printf("Blocked Dense LU Factorization\n");
  printf("     %ld by %ld Matrix\n",n,n);
  printf("     %ld Processors\n",P);
  printf("     %ld by %ld Element Blocks\n",block_size,block_size);
  printf("\n");
  printf("\n");

  // AYDIN: num_rows and num_cols are not used at the moment.
  num_rows = (long) sqrt((double) P);
  for (;;) {
    num_cols = P/num_rows;
    if (num_rows*num_cols == P)
      break;
    num_rows--;
  }



  nblocks = n/block_size;
  if (block_size * nblocks != n) {
    nblocks++;
  }

  // a = (double *) malloc(n*n*sizeof(double));
  const int ret = posix_memalign((void **)(&a), CACHELINE_SIZE, n*n*sizeof(double));
  assert(ret == 0);

  if (a == NULL) {
	  printerr("Could not malloc memory for a.\n");
	  exit(-1);
  }
  rhs = (double *) malloc(n*sizeof(double));;
  if (rhs == NULL) {
	  printerr("Could not malloc memory for rhs.\n");
	  exit(-1);
  }

  Global = (struct GlobalMemory *) malloc(sizeof(struct GlobalMemory));;
  Global->t_in_fac = (double *) malloc(P*sizeof(double));;
  Global->t_in_mod = (double *) malloc(P*sizeof(double));;
  Global->t_in_solve = (double *) malloc(P*sizeof(double));;
  Global->t_in_bar = (double *) malloc(P*sizeof(double));;
  Global->completion = (double *) malloc(P*sizeof(double));;

  if (Global == NULL) {
    printerr("Could not malloc memory for Global\n");
    exit(-1);
  } else if (Global->t_in_fac == NULL) {
    printerr("Could not malloc memory for Global->t_in_fac\n");
    exit(-1);
  } else if (Global->t_in_mod == NULL) {
    printerr("Could not malloc memory for Global->t_in_mod\n");
    exit(-1);
  } else if (Global->t_in_solve == NULL) {
    printerr("Could not malloc memory for Global->t_in_solve\n");
    exit(-1);
  } else if (Global->t_in_bar == NULL) {
    printerr("Could not malloc memory for Global->t_in_bar\n");
    exit(-1);
  } else if (Global->completion == NULL) {
    printerr("Could not malloc memory for Global->completion\n");
    exit(-1);
  }

/* POSSIBLE ENHANCEMENT:  Here is where one might distribute the a
   matrix data across physically distributed memories in a 
   round-robin fashion as desired. */

  {
	pthread_mutex_init(&((Global->start).bar_mutex), NULL);
	pthread_cond_init(&((Global->start).bar_cond), NULL);
	(Global->start).bar_teller=0;
};
  {pthread_mutex_init(&(Global->idlock),NULL);};
  Global->id = 0;

  InitA(rhs);
  if (doprint) {
    printf("Matrix before decomposition:\n");
    PrintA();
  }


  std::cout << "base cores: ";
    std::vector<int> base_assigned_cores;
    for (int i = 0; i < getCoreCount(); ++i)
    {
        if (i % 2 == 0)
        {
            base_assigned_cores.push_back(i);
            std::cout << i << ' ';
            // this is to bind cores in socket-0. all cores are even numbered in this socket.
        }
    }
    std::cout << std::endl;
    assert(base_assigned_cores.size() == P);  

  // ADDRESS-THREAD_ID TRACKING STARTS HERE.
  std::cout << "Starting address tracking..." << std::endl;
  const auto address_tracking_start = high_resolution_clock::now();
	assert(__threads__<__MAX_THREADS__);
	pthread_mutex_lock(&__intern__);
	for (int i = 0; i < (P) - 1; i++) {
		const int Error = pthread_create(&__tid__[__threads__++], NULL, SlaveStart, static_cast<void*>(base_assigned_cores.data()));
		if (Error != 0) {
			printf("Error in pthread_create().\n");
			exit(-1);
		}
	}
	pthread_mutex_unlock(&__intern__);

	SlaveStart(static_cast<void*>(base_assigned_cores.data()));
  std::cout << "WAITING FOR JOIN..." << std::endl;
  {int aantal=P; while (aantal--) pthread_join(__tid__[aantal], NULL);};
  std::cout << "AFTER JOIN" << std::endl;  

  const auto address_tracking_end = high_resolution_clock::now();
  std::cout << "Ended address tracking. elapsed time: " << duration_cast<milliseconds>(address_tracking_end - address_tracking_start).count() << "ms" << std::endl;
  Global->id = 0; // reset the id.
  __threads__ = 0; // reset this, too.
  (Global->start).bar_teller=0; // reset.
  InitA(rhs); // reset.

  // ADDRESS-THREAD_ID TRACKING IS DONE.




  // ALGO HERE.
  std::cout << "Starting preprocesing algo..." << std::endl;
  const auto algo_start = high_resolution_clock::now();


    assert(P > 1);  // below algo depends on this. we will find thread pairs.
    auto head = threadid_addresses_map.begin();
    auto tail = std::next(threadid_addresses_map.begin());

    // this is ranked_communication_count_per_pair wrt spmv repo.
    multiset<tuple<int, int, int>, greater<>>
        total_comm_count_t1_t2;  // set should suffice (compared to multiset). no tuple will be
                                 // the same since thread pairs are unique at this point here. but now, will make it multiset
    std::multiset<tuple<int, int, int, int>, greater<>> total_cha_freq_count_t1_t2;

    // map<pair<int, int>, multiset<Cell *>> pairing_addresses;
    while (head != threadid_addresses_map.end()) {
        const auto orig_tail = tail;
        while (tail != threadid_addresses_map.end()) {
            const int t1 = head->first;
            const int t2 = tail->first;
            // cout << "head: " << t1 << ", tail: " << t2 << endl;

            const multiset<double *> t1_addresses = head->second;
            const multiset<double *> t2_addresses = tail->second;

            std::multiset<double *> common_addresses;
            std::set_intersection(t1_addresses.begin(), t1_addresses.end(), t2_addresses.begin(), t2_addresses.end(),
                                  std::inserter(common_addresses, common_addresses.begin()));

            std::unordered_map<int, int> cha_freq_map;

            // this part is changed wrt fluidanimate.
            for(const double* common_addr : common_addresses) {
              ++cha_freq_map[findCha(common_addr)];
            }
            // this part is changed wrt fluidanimate.


            for(const auto& [cha, freq] : cha_freq_map) {
                total_cha_freq_count_t1_t2.insert({freq, cha, t1, t2});
            }

            total_comm_count_t1_t2.insert({common_addresses.size(), t1, t2});
            
            // pairing_addresses[{t1, t2}] = common_addresses; // pairing is not used at the moment. here just for clarity.
            ++tail;
        }
        tail = std::next(orig_tail);
        ++head;
    }

    // for (const auto &[thread_pairs, common_addresses] : pairing_addresses) {
    //     const auto t1 = thread_pairs.first;
    //     const auto t2 = thread_pairs.second;
    //     const auto common_address_count = common_addresses.size();
    //     std::cout << "threads " << t1 << " and " << t2 << " have " << common_address_count << " common addresses"
    //               << endl;
    //     total_comm_count_t1_t2.insert({common_address_count, t1, t2});
    // }



    // for (const auto &[total_comm_count, t1, t2] : total_comm_count_t1_t2) {
    //     std::cout << "total comm count: " << total_comm_count << ", t1: " << t1 << ", t2: " << t2 << endl;
    // }
    // for (const auto &[freq, cha, t1, t2] : total_cha_freq_count_t1_t2) {
    //     std::cout << "freq: " << freq << ", cha: " << cha << ", t1: " << t1 << ", t2: " << t2 << endl;
    // }    

    int mapped_thread_count = 0;
    auto it = total_cha_freq_count_t1_t2.begin();
    auto it1 = total_comm_count_t1_t2.begin();
    std::vector<int> thread_to_core(P, -1);

    // fprintf(stderr, "before topology creation\n");
    auto topo = Topology(cha_core_map, CAPID6);
    std::vector<Tile> mapped_tiles;
    // SPDLOG_TRACE("~~~~~~~~~~~~~~~~");
    //  fprintf(stderr, "before thread mapping creation\n");

    // start
    // it = ranked_cha_access_count_per_pair.begin();
    while (mapped_tiles.size() < P &&
           /*it != ranked_cha_access_count_per_pair.end()*/ it1 != total_comm_count_t1_t2.end()) {
        // std::pair<int, int> tid_pair(std::get<2>(*it), std::get<3>(*it));
        std::pair<int, int> tid_pair(std::get<1>(*it1), std::get<2>(*it1));
        if (thread_to_core[tid_pair.first] == -1 && thread_to_core[tid_pair.second] == -1) {
            // SPDLOG_TRACE("cha with max access: {}", std::get<1>(*it));
            int cha_id = getMostAccessedCHA(tid_pair.first, tid_pair.second, total_cha_freq_count_t1_t2, topo);
            if (cha_id == -1) {
                // SPDLOG_INFO("error: cha is -1");
                it1++;
                continue;
            }
            // auto tile = topo.getTile(std::get<1>(*it));
            auto tile = topo.getTile(cha_id);
            // SPDLOG_TRACE("cha {}, is colocated with core {}", cha_id, tile.core);
            // if (thread_to_core[tid_pair.first] == -1)
            {
                // SPDLOG_INFO("fetching a tile closest to tile with cha {} and core {}, cha supposed to be {}",
                // tile.cha, tile.core, std::get<1>(*it));
                auto closest_tile = topo.getClosestTile(tile, mapped_tiles);
                // auto closest_tile = topo.getClosestTilewithThreshold(tile, mapped_tiles);
                // SPDLOG_TRACE("* closest _available_ core to cha {} is: {}", tile.cha, closest_tile.core);
                mapped_tiles.push_back(closest_tile);
                thread_to_core[tid_pair.first] = closest_tile.core;
                // SPDLOG_TRACE("assigned thread with id {} to core {}", tid_pair.first, closest_tile.core);
            }
#if 0
            else
            {
                SPDLOG_TRACE("--> Already assigned thread with id {} to core {}, skipping it.", tid_pair.first, thread_to_core[tid_pair.first]);
            }
#endif

            // if (thread_to_core[tid_pair.second] == -1)
            {
                // SPDLOG_INFO("fetching a tile closest to tile with cha {} and core {}, cha supposed to be {}",
                // tile.cha, tile.core, std::get<1>(*it));
                auto closest_tile = topo.getClosestTile(tile, mapped_tiles);
                // auto closest_tile = topo.getClosestTilewithThreshold(tile, mapped_tiles);
                // SPDLOG_TRACE("# closest _available_ core to cha {} is: {}", tile.cha, closest_tile.core);
                mapped_tiles.push_back(closest_tile);
                thread_to_core[tid_pair.second] = closest_tile.core;
                // SPDLOG_TRACE("assigned thread with id {} to core {}", tid_pair.second, closest_tile.core);
            }
#if 0
            else
            {
                SPDLOG_TRACE("--> Already assigned thread with id {} to core {}, skipping it.", tid_pair.second, thread_to_core[tid_pair.second]);
            }
#endif
        }
        //#if 0
        else if (thread_to_core[tid_pair.first] == -1) {
            auto tile = topo.getTileByCore(thread_to_core[tid_pair.second]);
            auto closest_tile = topo.getClosestTile(tile, mapped_tiles);
            mapped_tiles.push_back(closest_tile);
            thread_to_core[tid_pair.first] = closest_tile.core;
        } else if (thread_to_core[tid_pair.second] == -1) {
            auto tile = topo.getTileByCore(thread_to_core[tid_pair.first]);
            auto closest_tile = topo.getClosestTile(tile, mapped_tiles);
            mapped_tiles.push_back(closest_tile);
            thread_to_core[tid_pair.second] = closest_tile.core;
        }
        //#endif

        it1++;
    }
    // end


    const auto algo_end = high_resolution_clock::now();
    std::cout << "Ended preprocesing algo. elapsed time: " << duration_cast<milliseconds>(algo_end - algo_start).count() << "ms" << std::endl;

    int ii = 0;
    for (auto ptr : thread_to_core) {
        std::cout << "thread " << i << " is mapped to core " << ptr << std::endl;
        // SPDLOG_INFO("thread {} is mapped to core {} ", ii, ptr);
        ii++;
    }

    assert(thread_to_core.size() == P);
    topo.printTopology();




  // cha aware BM.
  std::cout << "Now running cha aware BM" << std::endl;
	assert(__threads__<__MAX_THREADS__);

  const auto cha_aware_start = high_resolution_clock::now();
	pthread_mutex_lock(&__intern__);
	for (int i = 0; i < (P) - 1; i++) {
		const int Error = pthread_create(&__tid__[__threads__++], NULL, SlaveStart, static_cast<void*>(thread_to_core.data()));
		if (Error != 0) {
			printf("Error in pthread_create().\n");
			exit(-1);
		}
	}
	pthread_mutex_unlock(&__intern__);

	SlaveStart(static_cast<void*>(thread_to_core.data()));


  // std::cout << "WAITING FOR JOIN..." << std::endl;
  {int aantal=P; while (aantal--) pthread_join(__tid__[aantal], NULL);};
  // std::cout << "AFTER JOIN. ended cha aware bm" << std::endl;

  const auto cha_aware_end = high_resolution_clock::now();
  const auto elapsed_cha_aware = duration_cast<milliseconds>(cha_aware_end - cha_aware_start).count();
  std::cout << "Ended cha aware BM. elapsed time: " << elapsed_cha_aware << "ms" << std::endl;

  Global->id = 0; // reset the id.
  __threads__ = 0; // reset this, too.
  (Global->start).bar_teller=0; // reset.
  InitA(rhs); // reset.
  // END OF cha aware BM.


  




  // base BM.
  std::cout << "Now running base BM" << std::endl;
	assert(__threads__<__MAX_THREADS__);

  const auto base_start = high_resolution_clock::now();
	pthread_mutex_lock(&__intern__);
	for (int i = 0; i < (P) - 1; i++) {
		const int Error = pthread_create(&__tid__[__threads__++], NULL, SlaveStart, static_cast<void*>(base_assigned_cores.data()));
		if (Error != 0) {
			printf("Error in pthread_create().\n");
			exit(-1);
		}
	}
	pthread_mutex_unlock(&__intern__);

	SlaveStart(static_cast<void*>(base_assigned_cores.data()));


  // std::cout << "WAITING FOR JOIN..." << std::endl;
  {int aantal=P; while (aantal--) pthread_join(__tid__[aantal], NULL);};
  // std::cout << "AFTER JOIN. ended base bm" << std::endl;

  const auto base_end = high_resolution_clock::now();
  const auto elapsed_base = duration_cast<milliseconds>(base_end - base_start).count();
  std::cout << "Ended base BM. elapsed time: " << elapsed_base << "ms" << std::endl;


  // NO NEED TO RESET FROM NOW ON!
  // Global->id = 0; // reset the id.
  // __threads__ = 0; // reset this, too.
  // (Global->start).bar_teller=0; // reset.
  // InitA(rhs); // reset.

  // END OF base BM.





  std::cout << "latency improv percentage: " << ((elapsed_base - elapsed_cha_aware) / static_cast<double>(elapsed_base)) * 100 << std::endl;



  if (doprint) {
    printf("\nMatrix after decomposition:\n");
    PrintA();
  }

  if (dostats) {
    maxt = avgt = mint = Global->completion[0];
    for (i=1; i<P; i++) {
      if (Global->completion[i] > maxt) {
        maxt = Global->completion[i];
      }
      if (Global->completion[i] < mint) {
        mint = Global->completion[i];
      }
      avgt += Global->completion[i];
    }
    avgt = avgt / P;

    min_fac = max_fac = avg_fac = Global->t_in_fac[0];
    min_solve = max_solve = avg_solve = Global->t_in_solve[0];
    min_mod = max_mod = avg_mod = Global->t_in_mod[0];
    min_bar = max_bar = avg_bar = Global->t_in_bar[0];

    for (i=1; i<P; i++) {
      if (Global->t_in_fac[i] > max_fac) {
        max_fac = Global->t_in_fac[i];
      }
      if (Global->t_in_fac[i] < min_fac) {
        min_fac = Global->t_in_fac[i];
      }
      if (Global->t_in_solve[i] > max_solve) {
        max_solve = Global->t_in_solve[i];
      }
      if (Global->t_in_solve[i] < min_solve) {
        min_solve = Global->t_in_solve[i];
      }
      if (Global->t_in_mod[i] > max_mod) {
        max_mod = Global->t_in_mod[i];
      }
      if (Global->t_in_mod[i] < min_mod) {
        min_mod = Global->t_in_mod[i];
      }
      if (Global->t_in_bar[i] > max_bar) {
        max_bar = Global->t_in_bar[i];
      }
      if (Global->t_in_bar[i] < min_bar) {
        min_bar = Global->t_in_bar[i];
      }
      avg_fac += Global->t_in_fac[i];
      avg_solve += Global->t_in_solve[i];
      avg_mod += Global->t_in_mod[i];
      avg_bar += Global->t_in_bar[i];
    }
    avg_fac = avg_fac/P;
    avg_solve = avg_solve/P;
    avg_mod = avg_mod/P;
    avg_bar = avg_bar/P;
  }
  printf("                            PROCESS STATISTICS\n");
  printf("              Total      Diagonal     Perimeter      Interior       Barrier\n");
  printf(" Proc         Time         Time         Time           Time          Time\n");
  printf("    0    %10.0f    %10.0f    %10.0f    %10.0f    %10.0f\n",
          Global->completion[0],Global->t_in_fac[0],
          Global->t_in_solve[0],Global->t_in_mod[0],
          Global->t_in_bar[0]);
  if (dostats) {
    for (i=1; i<P; i++) {
      printf("  %3ld    %10.0f    %10.0f    %10.0f    %10.0f    %10.0f\n",
              i,Global->completion[i],Global->t_in_fac[i],
              Global->t_in_solve[i],Global->t_in_mod[i],
              Global->t_in_bar[i]);
    }
    printf("  Avg    %10.0f    %10.0f    %10.0f    %10.0f    %10.0f\n",
           avgt,avg_fac,avg_solve,avg_mod,avg_bar);
    printf("  Min    %10.0f    %10.0f    %10.0f    %10.0f    %10.0f\n",
           mint,min_fac,min_solve,min_mod,min_bar);
    printf("  Max    %10.0f    %10.0f    %10.0f    %10.0f    %10.0f\n",
           maxt,max_fac,max_solve,max_mod,max_bar);
  }
  printf("\n");
  Global->starttime = start;
  printf("                            TIMING INFORMATION\n");
  printf("Start time                        : %16lu\n", Global->starttime);
  printf("Initialization finish time        : %16lu\n", Global->rs);
  printf("Overall finish time               : %16lu\n", Global->rf);
  printf("Total time with initialization    : %16lu\n", Global->rf-Global->starttime);
  printf("Total time without initialization : %16lu\n", Global->rf-Global->rs);
  printf("\n");

  if (test_result) {
    printf("                             TESTING RESULTS\n");
    CheckResult(n, a, rhs);
  }

  {exit(0);};
}

void* SlaveStart(void* data)
{
  assert(data);

  int* cores = static_cast<int*>(data);
  // std::cout << "cores: ";
  // for(int i = 0; i < 28; ++i) {
  //   std::cout << i << ' ';
  // }
  // std::cout << std::endl;
  // return nullptr;

  long MyNum;

  {pthread_mutex_lock(&(Global->idlock));}
    MyNum = Global->id;
    Global->id ++;
  {pthread_mutex_unlock(&(Global->idlock));}

  // std::cout << "id: " << MyNum << ", core: " << cores[static_cast<int>(MyNum)] << std::endl;

  stick_this_thread_to_core(cores[static_cast<int>(MyNum)]);

  OneSolve(n, block_size, MyNum, dostats);

  // std::cout << "END OF THREAD: #" << MyNum << std::endl;
  return nullptr;
}


void OneSolve(long n, long block_size, long MyNum, long dostats)
{
  unsigned long myrs, myrf, mydone;
  struct LocalCopies *lc;

  lc = (struct LocalCopies *) malloc(sizeof(struct LocalCopies));
  if (lc == NULL) {
    fprintf(stderr,"Proc %ld could not malloc memory for lc\n",MyNum);
    exit(-1);
  }
  lc->t_in_fac = 0.0;
  lc->t_in_solve = 0.0;
  lc->t_in_mod = 0.0;
  lc->t_in_bar = 0.0;

  /* barrier to ensure all initialization is done */
  {
pthread_mutex_lock(&((Global->start).bar_mutex));
(Global->start).bar_teller++;
if ((Global->start).bar_teller == (P)) {
	(Global->start).bar_teller = 0;
	pthread_cond_broadcast(&((Global->start).bar_cond));
} else {
	pthread_cond_wait(&((Global->start).bar_cond), &((Global->start).bar_mutex));
}
pthread_mutex_unlock(&((Global->start).bar_mutex));}
;

  /* to remove cold-start misses, all processors begin by touching a[] */
  TouchA(block_size, MyNum);

  {
pthread_mutex_lock(&((Global->start).bar_mutex));
(Global->start).bar_teller++;
if ((Global->start).bar_teller == (P)) {
	(Global->start).bar_teller = 0;
	pthread_cond_broadcast(&((Global->start).bar_cond));
} else {
	pthread_cond_wait(&((Global->start).bar_cond), &((Global->start).bar_mutex));
}
pthread_mutex_unlock(&((Global->start).bar_mutex));}
;

/* POSSIBLE ENHANCEMENT:  Here is where one might reset the
   statistics that one is measuring about the parallel execution */

  if ((MyNum == 0) || (dostats)) {
    {long time{}; (myrs) = ::time(0);};
  }

  lu(n, block_size, MyNum, lc, dostats);

  if ((MyNum == 0) || (dostats)) {
    {long time{}; (mydone) = ::time(0);};
  }

  {
pthread_mutex_lock(&((Global->start).bar_mutex));
(Global->start).bar_teller++;
if ((Global->start).bar_teller == (P)) {
	(Global->start).bar_teller = 0;
	pthread_cond_broadcast(&((Global->start).bar_cond));
} else {
	pthread_cond_wait(&((Global->start).bar_cond), &((Global->start).bar_mutex));
}
pthread_mutex_unlock(&((Global->start).bar_mutex));}
;

  if ((MyNum == 0) || (dostats)) {
    {long time{}; (myrf) = ::time(0);};
    Global->t_in_fac[MyNum] = lc->t_in_fac;
    Global->t_in_solve[MyNum] = lc->t_in_solve;
    Global->t_in_mod[MyNum] = lc->t_in_mod;
    Global->t_in_bar[MyNum] = lc->t_in_bar;
    Global->completion[MyNum] = mydone-myrs;
  }
  if (MyNum == 0) {
    Global->rs = myrs;
    Global->done = mydone;
    Global->rf = myrf;
  }
}


void lu0(double *a, long n, long stride, long MyNum)
{
  long j, k, length;
  double alpha;

  for (k=0; k<n; k++) {
    /* modify subsequent columns */
    for (j=k+1; j<n; j++) {
      a[k+j*stride] /= a[k+k*stride]; // a written

      {
        std::lock_guard lock(map_mutex);
        threadid_addresses_map[MyNum].insert(&a[k+j*stride]);
      }

      alpha = -a[k+j*stride];
      length = n-k-1;
      daxpy(&a[k+1+j*stride], &a[k+1+k*stride], n-k-1, alpha, MyNum);
    }
  }
}


void bdiv(double *a, double *diag, long stride_a, long stride_diag, long dimi, long dimk, long MyNum)
{
  long j, k;
  double alpha;

  for (k=0; k<dimk; k++) {
    for (j=k+1; j<dimk; j++) {
      alpha = -diag[k+j*stride_diag];
      daxpy(&a[j*stride_a], &a[k*stride_a], dimi, alpha, MyNum);
    }
  }
}


void bmodd(double *a, double *c, long dimi, long dimj, long stride_a, long stride_c, long MyNum)
{
  long j, k, length;
  double alpha;

  for (k=0; k<dimi; k++)
    for (j=0; j<dimj; j++) {
      c[k+j*stride_c] /= a[k+k*stride_a]; // a written

      {
        std::lock_guard lock(map_mutex);
        threadid_addresses_map[MyNum].insert(&c[k+j*stride_c]);
      }

      alpha = -c[k+j*stride_c];
      length = dimi - k - 1;
      daxpy(&c[k+1+j*stride_c], &a[k+1+k*stride_a], dimi-k-1, alpha, MyNum);
    }
}


void bmod(double *a, double *b, double *c, long dimi, long dimj, long dimk, long stride, long MyNum)
{
  long j, k;
  double alpha;

  for (k=0; k<dimk; k++) {
    for (j=0; j<dimj; j++) {
      alpha = -b[k+j*stride];
      daxpy(&c[j*stride], &a[k*stride], dimi, alpha, MyNum);
    }
  }
}


void daxpy(double *a, double *b, long n, double alpha, long MyNum)
{
  long i;

  for (i=0; i<n; i++) {
    a[i] += alpha*b[i]; // a written

    {
      std::lock_guard lock(map_mutex);
      threadid_addresses_map[MyNum].insert(&a[i]);
    }
  }
}


long BlockOwner(long I, long J)
{
//	return((I%num_cols) + (J%num_rows)*num_cols);
	return((I + J*nblocks) % P);
}

long BlockOwnerColumn(long I, long J)
{
	return(I % P);
}

long BlockOwnerRow(long I, long J)
{
	return(((J % P) + (P / 2)) % P);
}

void lu(long n, long bs, long MyNum, struct LocalCopies *lc, long dostats)
{
  long i, il, j, jl, k, kl, I, J, K;
  double *A, *B, *C, *D; // AYDIN: these will be assigned to addresses of A. so, treat accesses to these as accesses to A.
  long strI;
  unsigned long t1, t2, t3, t4, t11, t22;

  strI = n;
  for (k=0, K=0; k<n; k+=bs, K++) {
    kl = k+bs; 
    if (kl>n) {
      kl = n;
    }

    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t1) = ::time(0);};
    }

    /* factor diagonal block */
    if (BlockOwner(K, K) == MyNum) {
      A = &(a[k+k*n]); 
      lu0(A, kl-k, strI, MyNum);
    }

    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t11) = ::time(0);};
    }

    {
pthread_mutex_lock(&((Global->start).bar_mutex));
(Global->start).bar_teller++;
if ((Global->start).bar_teller == (P)) {
	(Global->start).bar_teller = 0;
	pthread_cond_broadcast(&((Global->start).bar_cond));
} else {
	pthread_cond_wait(&((Global->start).bar_cond), &((Global->start).bar_mutex));
}
pthread_mutex_unlock(&((Global->start).bar_mutex));}
;

    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t2) = ::time(0);};
    }

    /* divide column k by diagonal block */
    D = &(a[k+k*n]);
    for (i=kl, I=K+1; i<n; i+=bs, I++) {
      if (BlockOwner/*Column*/(I, K) == MyNum) {  /* parcel out blocks */
	      /*if (K == 0) printf("C%lx\n", BlockOwnerColumn(I, K));*/
        il = i + bs;
        if (il > n) {
          il = n;
        }
        A = &(a[i+k*n]);
        bdiv(A, D, strI, n, il-i, kl-k, MyNum);
      }
    }
    /* modify row k by diagonal block */
    for (j=kl, J=K+1; j<n; j+=bs, J++) {
      if (BlockOwner/*Row*/(K, J) == MyNum) {  /* parcel out blocks */
	      /*if (K == 0) printf("R%lx\n", BlockOwnerRow(K, J));*/
        jl = j+bs;
        if (jl > n) {
          jl = n;
        }
        A = &(a[k+j*n]);
        bmodd(D, A, kl-k, jl-j, n, strI, MyNum);
      }
    }

    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t22) = ::time(0);};
    }

    {
pthread_mutex_lock(&((Global->start).bar_mutex));
(Global->start).bar_teller++;
if ((Global->start).bar_teller == (P)) {
	(Global->start).bar_teller = 0;
	pthread_cond_broadcast(&((Global->start).bar_cond));
} else {
	pthread_cond_wait(&((Global->start).bar_cond), &((Global->start).bar_mutex));
}
pthread_mutex_unlock(&((Global->start).bar_mutex));}
;

    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t3) = ::time(0);};
    }

    /* modify subsequent block columns */
    for (i=kl, I=K+1; i<n; i+=bs, I++) {
      il = i+bs;
      if (il > n) {
        il = n;
      }
      A = &(a[i+k*n]);
      for (j=kl, J=K+1; j<n; j+=bs, J++) {
        jl = j + bs;
        if (jl > n) {
          jl = n;
        }
        if (BlockOwner(I, J) == MyNum) {  /* parcel out blocks */
//		if (K == 0) printf("%lx\n", BlockOwner(I, J));
          B = &(a[k+j*n]);
          C = &(a[i+j*n]);
          bmod(A, B, C, il-i, jl-j, kl-k, n, MyNum);
        }
      }
    }
    if ((MyNum == 0) || (dostats)) {
      {long time{}; (t4) = ::time(0);};
      lc->t_in_fac += (t11-t1);
      lc->t_in_solve += (t22-t2);
      lc->t_in_mod += (t4-t3);
      lc->t_in_bar += (t2-t11) + (t3-t22);
    }
  }
}


void InitA(double *rhs)
{
  long i, j;

  srand48((long) 1);
  for (j=0; j<n; j++) {
    for (i=0; i<n; i++) {
      a[i+j*n] = (double) lrand48()/MAXRAND;
      if (i == j) {
	a[i+j*n] *= 10;
      }
    }
  }

  for (j=0; j<n; j++) {
    rhs[j] = 0.0;
  }
  for (j=0; j<n; j++) {
    for (i=0; i<n; i++) {
      rhs[i] += a[i+j*n];
    }
  }
}


double TouchA(long bs, long MyNum)
{
  long i, j, I, J;
  double tot = 0.0;

  for (J=0; J*bs<n; J++) {
    for (I=0; I*bs<n; I++) {
      if (BlockOwner(I, J) == MyNum) {
        for (j=J*bs; j<(J+1)*bs && j<n; j++) {
          for (i=I*bs; i<(I+1)*bs && i<n; i++) {
            tot += a[i+j*n];
          }
        }
      }
    }
  }
  return(tot);
}


void PrintA()
{
  long i, j;

  for (i=0; i<n; i++) {
    for (j=0; j<n; j++) {
      printf("%8.1f ", a[i+j*n]);
    }
    printf("\n");
  }
}


void CheckResult(long n, double *a, double *rhs)
{
  long i, j, bogus = 0;
  double *y, diff, max_diff;

  y = (double *) malloc(n*sizeof(double));
  if (y == NULL) {
    printerr("Could not malloc memory for y\n");
    exit(-1);
  }
  for (j=0; j<n; j++) {
    y[j] = rhs[j];
  }
  for (j=0; j<n; j++) {
    y[j] = y[j]/a[j+j*n];
    for (i=j+1; i<n; i++) {
      y[i] -= a[i+j*n]*y[j];
    }
  }

  for (j=n-1; j>=0; j--) {
    for (i=0; i<j; i++) {
      y[i] -= a[i+j*n]*y[j];
    }
  }

  max_diff = 0.0;
  for (j=0; j<n; j++) {
    diff = y[j] - 1.0;
    if (fabs(diff) > 0.00001) {
      bogus = 1;
      max_diff = diff;
    }
  }
  if (bogus) {
    printf("TEST FAILED: (%.5f diff)\n", max_diff);
  } else {
    printf("TEST PASSED\n");
  }
  free(y);
}


void printerr(const char *s)
{
  fprintf(stderr,"ERROR: %s\n",s);
}
