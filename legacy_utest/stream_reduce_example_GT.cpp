/**
* @version		GrPPI v0.2
* @copyright		Copyright (C) 2017 Universidad Carlos III de Madrid. All rights reserved.
* @license		GNU/GPL, see LICENSE.txt
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You have received a copy of the GNU General Public License in LICENSE.txt
* also available in <http://www.gnu.org/licenses/gpl.html>.
*
* See COPYRIGHT.txt for copyright notices and details.
*/
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <stream_reduce.h>
#include <string>
#include <sstream>
#include <gtest/gtest.h>
using namespace grppi;

std::vector<int> read_list(std::istream & is){
  using namespace std;
  std::vector<int> result;
  string line;
  is >> ws;
  if(!getline(is,line)) return result;
  istringstream iline{line};
  int x;
  while(iline >> x){
    result.push_back(x);
  }
  return result;
}



int stream_reduce_example(auto &p) {
    using namespace std;
 
#ifndef NTHREADS
#define NTHREADS 6
#endif

    ifstream is("txt/file.txt");
    if (!is.good()) { cerr << "TXT file not found!" << endl; return 0; }

    int reduce_var = 0;
    
    stream_reduce( p,
        // GenFunc: stream consumer
        [&]() {
            auto r = read_list(is);
            return ( r.size() == 0 ) ? optional<vector<int>>{} : optional<vector<int>>(r);
        },  	
        // TaskFunc: reduce kernel 
        [&]( vector<int> v ) {
            int loc_red = 0;
            for( int i = 0; i < v.size(); i++ )
                loc_red += v[i];
            return loc_red;
        },
        // RedFunc: final reduce
        [&]( int loc_red ,int &reduce_var ) {
            reduce_var += loc_red;
    }, reduce_var
);

    return reduce_var;
}


TEST(GrPPI, stream_reduce_example_seq ){
    sequential_execution p{};
    EXPECT_EQ(5060408, stream_reduce_example(p) );
}

TEST(GrPPI, stream_reduce_example_thr ){
    parallel_execution_thr p{NTHREADS};
    EXPECT_EQ(5060408, stream_reduce_example(p) );
}

#ifdef GRPPI_OMP
    TEST(GrPPI, stream_reduce_example_omp ){
        parallel_execution_omp p{NTHREADS};
        EXPECT_EQ(5060408, stream_reduce_example(p) );
    }
#endif
#ifdef GRPPI_TBB
    TEST(GrPPI, stream_reduce_example_tbb ){
        parallel_execution_tbb p{NTHREADS};
        EXPECT_EQ(5060408, stream_reduce_example(p) );
    }
#endif



int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
