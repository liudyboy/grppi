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

#ifndef GRPPI_OMP_PARALLEL_EXECUTION_OMP_H
#define GRPPI_OMP_PARALLEL_EXECUTION_OMP_H

// Only if compiled with OpenMP enabled
#ifdef GRPPI_OMP

#include <type_traits>

#include <omp.h>

#include "common/mpmc_queue.h"

namespace grppi{

/** @brief Set the execution mode to parallel with ompenmp framework 
 *    implementation 
 */
struct parallel_execution_omp{
  constexpr static int default_queue_size = 100;
  int queue_size = default_queue_size;
  queue_mode lockfree = queue_mode::blocking;

  void set_queue_size(int new_size){
     queue_size = new_size;
  }


  int get_thread_id(){
     return omp_get_thread_num();
  }
  /** @brief Set num_threads to the maximum number of thread available by the
   *    hardware
   */
  parallel_execution_omp(){};

  /** @brief Set num_threads to _threads in order to run in parallel
   *
   *  @param _threads number of threads used in the parallel mode
   */
  parallel_execution_omp(int _threads){num_threads=_threads; };

  /** @brief Set num_threads to _threads in order to run in parallel and allows to disable the ordered execution
   *
   *  @param _threads number of threads used in the parallel mode
   *  @param _order enable or disable the ordered execution
   */
  parallel_execution_omp(int _threads, bool order){ num_threads=_threads; ordering = order;};

  /**
  \brief Set number of grppi threads.
  */
  void set_concurrency_degree(int degree) noexcept { num_threads = degree; }

  /**
  \brief Get number of grppi trheads.
  */
  int concurrency_degree() const noexcept { return num_threads; }

  /**
  \brief Enable ordering.
  */
  void enable_ordering() noexcept { ordering=true; }

  /**
  \brief Disable ordering.
  */
  void disable_ordering() noexcept { ordering=false; }

  /**
  \brief Is execution ordered.
  */
  bool is_ordered() const noexcept { return ordering; }


  private:
    constexpr static int default_num_threads = 4;
    int num_threads = default_num_threads;
    bool ordering = true;
};

/// Determine if a type is an OMP execution policy.
template <typename E>
constexpr bool is_parallel_execution_omp() {
  return std::is_same<E, parallel_execution_omp>::value;
}

template <typename E>
constexpr bool is_supported();

template <>
constexpr bool is_supported<parallel_execution_omp>() {
  return true;
}

} // end namespace grppi

#else // GRPPI_OMP undefined

namespace grppi {

/// Parallel execution policy.
/// Empty type if GRPPI_OMP disabled.
struct parallel_execution_omp {};

/// Determine if a type is an OMP execution policy.
/// False if GRPPI_OMP disabled.
template <typename E>
constexpr bool is_parallel_execution_omp() {
  return false;
}

template <typename E>
constexpr bool is_supported();

template <>
constexpr bool is_supported<parallel_execution_omp>() {
  return false;
}

}

#endif // GRPPI_OMP

#endif
