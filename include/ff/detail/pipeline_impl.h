/**
* @version		GrPPI v0.3
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

#ifndef GRPPI_FF_DETAIL_PIPELINE_IMPL_H
#define GRPPI_FF_DETAIL_PIPELINE_IMPL_H

#ifdef GRPPI_FF

#include "ordered_stream_reduce.h"
#include "unordered_stream_reduce.h"
#include "ordered_stream_filter.h"
#include "unordered_stream_filter.h"
#include "iteration_worker.h"

#include <ff/allocator.hpp>
#include <ff/pipeline.hpp>
#include <ff/farm.hpp>

namespace grppi {

namespace detail_ff {

template <typename Input, typename Output, typename Transformer>
struct node_impl : ff::ff_node_t<Input,Output> {
  Transformer transform_op_;

  node_impl(Transformer && transform_op) : 
      transform_op_{transform_op}
  {}

  Output * svc(Input * p_item) {
    Output * p_out = static_cast<Output*>(ff::ff_malloc(sizeof(Output)));
    *p_out = transform_op_(*p_item);
    return p_out;
  } 
};

template <typename Output, typename Transformer>
struct node_impl<void,Output,Transformer> : ff::ff_node {
  Transformer transform_op_;

  node_impl(Transformer && transform_op) :
      transform_op_{transform_op}
  {}

  void * svc(void *) {
    std::experimental::optional<Output> result;
    void * p_out_buf = ff::ff_malloc(sizeof(Output));
    Output * p_out = new (p_out_buf) Output;
    result = transform_op_();
    if (result) {
      *p_out = result.value();
      return p_out_buf;
    }
    else {
      p_out->~Output();
      ff::ff_free(p_out_buf);
      return EOS;
    }
  }
};

template <typename Input, typename Transformer>
struct node_impl<Input,void,Transformer> : ff::ff_node_t<Input,void> {
  Transformer transform_op_;

  node_impl(Transformer && transform_op) :
      transform_op_{transform_op}
  {}

  void * svc(Input * p_item) {
    transform_op_(*p_item);
    p_item->~Input();
    ff::ff_free(p_item);
    return GO_ON;
  }

};


class pipeline_impl : public ff::ff_pipeline {
public:

  template <typename Generator, typename ... Transformers>
  pipeline_impl(int nworkers, bool ordered, Generator && gen, 
      Transformers && ... transform_ops);

  ~pipeline_impl() {
    for (auto p_stage : cleanup_stages_) {
      delete p_stage;
    }
  }

  operator ff_node*() { return this; }

private:

  void add_stage(ff_node & node) {
    ff::ff_pipeline::add_stage(&node);
  }

  void add_stage(ff_node * p_node) {
    cleanup_stages_.push_back(p_node);
    ff::ff_pipeline::add_stage(p_node);
  }

  template <typename T>
  void add_stages() {}

  template <typename Input, typename Transformer,
      requires_no_pattern<Transformer> = 0>
  auto add_stages(Transformer &&stage) 
  {
    using input_type = std::decay_t<Input>;

    auto p_stage = new node_impl<input_type,void,Transformer>(
        std::forward<Transformer>(stage));
    add_stage(p_stage);
  }

  template <typename Input, typename Transformer, typename ... OtherTransformers,
      requires_no_pattern<Transformer> = 0>
  auto add_stages(Transformer && transform_op,
      OtherTransformers && ... other_transform_ops) 
  {
    static_assert(!std::is_void<Input>::value,
        "Transformer must take non-void argument");
    using output_type =
        std::decay_t<typename std::result_of<Transformer(Input)>::type>;
    static_assert(!std::is_void<output_type>::value,
        "Transformer must return a non-void result");

    auto p_stage = new node_impl<Input,output_type,Transformer>(
        std::forward<Transformer>(transform_op));

    add_stage(p_stage);
    add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
  }

  template <typename Input, typename ... Transformers,
          template <typename...> class Pipeline,
          typename ... OtherTransformers,
          requires_pipeline<Pipeline<Transformers...>> = 0>
  auto add_stages(Pipeline<Transformers...> & pipeline_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages<Input>(std::move(pipeline_obj),
        std::forward<OtherTransformers>(other_transform_ops)...);
  }

  template <typename Input, typename ... Transformers,
          template <typename...> class Pipeline,
          typename ... OtherTransformers,
          requires_pipeline<Pipeline<Transformers...>> = 0>
  auto add_stages(Pipeline<Transformers...> && pipeline_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages_nested<Input>(
        std::tuple_cat(
          pipeline_obj.transformers(),
          std::forward_as_tuple(other_transform_ops...)
        ),
        std::make_index_sequence<sizeof...(Transformers)+sizeof...(OtherTransformers)>());
  }

  template <typename Input, typename ... Transformers, std::size_t ... I>
  auto add_stages_nested(std::tuple<Transformers...> && transform_ops,
      std::index_sequence<I...>) 
  {
    return add_stages<Input>(std::forward<Transformers>(std::get<I>(transform_ops))...);
  }

  template <typename Input, typename FarmTransformer,
          template <typename> class Farm,
          requires_farm<Farm<FarmTransformer>> = 0>
  auto add_stages(Farm<FarmTransformer> & farm_obj) 
  {
    return this->template add_stages<Input>(std::move(farm_obj));
  }

  template <typename Input, typename FarmTransformer,
          template <typename> class Farm,
          requires_farm<Farm<FarmTransformer>> = 0>
  auto add_stages(Farm<FarmTransformer> && farm_obj) 
  {
    static_assert(!std::is_void<Input>::value,
        "Farm must take non-void argument");
    using output_type = std::decay_t<typename std::result_of<
        FarmTransformer(Input)>::type>;

    using node_type = node_impl<Input,output_type,Farm<FarmTransformer>>;
    std::vector<std::unique_ptr<ff::ff_node>> workers;
    for(int i=0; i<nworkers_; ++i) {
      workers.push_back(std::make_unique<node_type>(
          std::forward<Farm<FarmTransformer>>(farm_obj))
      );
    }

    if(ordered_) {
      ff::ff_OFarm<Input,output_type> * p_farm =
          new ff::ff_OFarm<Input,output_type>(std::move(workers));
      add_stage(p_farm);
    } else 
    {
      ff::ff_Farm<Input,output_type> * p_farm =
          new ff::ff_Farm<Input,output_type>(std::move(workers));
      add_stage(p_farm);
    }
  }

  // parallel stage -- Farm pattern ref with variadic
  template <typename Input, typename FarmTransformer,
          template <typename> class Farm,
          typename ... OtherTransformers,
          requires_farm<Farm<FarmTransformer>> = 0>
  auto add_stages(Farm<FarmTransformer> & farm_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages<Input>(std::move(farm_obj),
        std::forward<OtherTransformers>(other_transform_ops)...);
  }

  // parallel stage -- Farm pattern with variadic
  template <typename Input, typename FarmTransformer,
          template <typename> class Farm,
          typename ... OtherTransformers,
          requires_farm<Farm<FarmTransformer>> = 0>
  auto add_stages( Farm<FarmTransformer> && farm_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    static_assert(!std::is_void<Input>::value,
        "Farm must take non-void argument");
    using output_type =
        std::decay_t<typename std::result_of<FarmTransformer(Input)>::type>;
    static_assert(!std::is_void<output_type>::value,
        "Farm must return a non-void result");

    using node_type = node_impl<Input,output_type,Farm<FarmTransformer>>;
    std::vector<std::unique_ptr<ff::ff_node>> workers;

    for(int i=0; i<nworkers_; ++i) {
      workers.push_back( std::make_unique<node_type>(
          std::forward<Farm<FarmTransformer>>(farm_obj))
      );
    }

    if(ordered_) {
      ff::ff_OFarm<Input,output_type> * p_farm =
          new ff::ff_OFarm<Input,output_type>(std::move(workers));
      add_stage(p_farm);
      add_stages<output_type>(std::forward<OtherTransformers>(other_transform_ops)...);
    } 
    else {
      ff::ff_Farm<Input,output_type> * p_farm =
          new ff::ff_Farm<Input,output_type>(std::move(workers));
      add_stage(p_farm);
      add_stages<output_type>(std::forward<OtherTransformers>(other_transform_ops)...);
    }
  }

  // parallel stage -- Filter pattern ref
  template <typename Input, typename Predicate,
      template <typename> class Filter,
      requires_filter<Filter<Predicate>> = 0>
  auto add_stages(Filter<Predicate> & filter_obj) 
  {
    return this->template add_stages<Input>(std::move(filter_obj));
  }

  // parallel stage -- Filter pattern
  template <typename Input, typename Predicate,
      template <typename> class Filter,
      requires_filter<Filter<Predicate>> = 0>
  auto add_stages(Filter<Predicate> && filter_obj) 
{
    static_assert(!std::is_void<Input>::value,
        "Filter must take non-void argument");

    if(ordered_) {
      ordered_stream_filter<Input,Filter<Predicate>> *theFarm =
          new ordered_stream_filter<Input,Filter<Predicate>>(
              std::forward<Filter<Predicate>>(filter_obj), nworkers_
          );
      add_stage(theFarm);
    } else {
      unordered_stream_filter<Input,Filter<Predicate>> *theFarm =
          new unordered_stream_filter<Input,Filter<Predicate>>(
              std::forward<Filter<Predicate>>(filter_obj), nworkers_
          );
      add_stage(theFarm);
    }
  }

  // parallel stage -- Filter pattern ref with variadics
  template <typename Input, typename Predicate,
      template <typename> class Filter,
      typename ... OtherTransformers,
      requires_filter<Filter<Predicate>> = 0>
  auto add_stages(Filter<Predicate> & filter_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages<Input>(std::move(filter_obj),
        std::forward<OtherTransformers>(other_transform_ops)...);
  }

  // parallel stage -- Filter pattern with variadics
  template <typename Input, typename Predicate,
      template <typename> class Filter,
      typename ... OtherTransformers,
      requires_filter<Filter<Predicate>> = 0>
  auto add_stages(Filter<Predicate> && filter_obj,
      OtherTransformers && ... other_transform_ops) {
    static_assert(!std::is_void<Input>::value,
        "Filter must take non-void argument");

    if(ordered_) {
      ordered_stream_filter<Input,Filter<Predicate>> * p_farm =
          new ordered_stream_filter<Input,Filter<Predicate>>{
              std::forward<Filter<Predicate>>(filter_obj), nworkers_};
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    } 
    else {
      unordered_stream_filter<Input,Filter<Predicate>> * p_farm =
          new unordered_stream_filter<Input,Filter<Predicate>>{
              std::forward<Filter<Predicate>>(filter_obj), nworkers_};
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    }
  }

  template <typename Input, typename Combiner, typename Identity,
          template <typename C, typename I> class Reduce,
          typename ... OtherTransformers,
          requires_reduce<Reduce<Combiner,Identity>> = 0>
  auto add_stages(Reduce<Combiner,Identity> & reduce_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages<Input>(std::move(reduce_obj),
        std::forward<OtherTransformers>(other_transform_ops)...);
  }

  template <typename Input, typename Combiner, typename Identity,
          template <typename C, typename I> class Reduce,
          typename ... OtherTransformers,
          requires_reduce<Reduce<Combiner,Identity>> = 0>
  auto add_stages(Reduce<Combiner,Identity> && reduce_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    static_assert(!std::is_void<Input>::value,
        "Reduce must take non-void argument");

    if(ordered_) {
      ordered_stream_reduce<Input,Reduce<Combiner,Identity>,Combiner> * p_farm =
          new ordered_stream_reduce<Input,Reduce<Combiner,Identity>, Combiner>{
              std::forward<Reduce<Combiner,Identity>>(reduce_obj), nworkers_};
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    } 
    else {
      unordered_stream_reduce<Input,Reduce<Combiner,Identity>,Combiner> * p_farm =
          new unordered_stream_reduce<Input,Reduce<Combiner,Identity>, Combiner>{
              std::forward<Reduce<Combiner,Identity>>(reduce_obj), nworkers_};
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    }
  }

  /**
  \brief Adds a stage with an iteration object.
  \note This version takes iteration by l-value reference.
  */
  template <typename Input, typename Transformer, typename Predicate,
            template <typename T, typename P> class Iteration,
            typename ... OtherTransformers,
            requires_iteration<Iteration<Transformer,Predicate>> =0,
            requires_no_pattern<Transformer> =0>
  auto add_stages(Iteration<Transformer,Predicate> & iteration_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    return this->template add_stages<Input>(std::move(iteration_obj),
        std::forward<OtherTransformers>(other_transform_ops)...);
  }


  /**
  \brief Adds a stage with an iteration object.
  \note This version takes iteration by r-value reference.
  */
  template <typename Input, typename Transformer, typename Predicate,
            template <typename T, typename P> class Iteration,
            typename ... OtherTransformers,
            requires_iteration<Iteration<Transformer,Predicate>> =0,
            requires_no_pattern<Transformer> =0>
  auto add_stages(Iteration<Transformer,Predicate> && iteration_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    std::vector<std::unique_ptr<ff::ff_node>> workers;

    for (int i=0; i<nworkers_; ++i)
      workers.push_back(
        std::make_unique<iteration_worker<Input,Iteration<Transformer,Predicate>>>(
          std::forward<Iteration<Transformer,Predicate>>(iteration_obj))
      );

    if (ordered_) {
      ff::ff_OFarm<Input> * p_farm = new ff::ff_OFarm<Input>(std::move(workers));
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    } 
    else {
      ff::ff_Farm<Input> * p_farm= new ff::ff_Farm<Input>(std::move(workers));
      add_stage(p_farm);
      add_stages<Input>(std::forward<OtherTransformers>(other_transform_ops)...);
    }
  }

  /**
  \brief Adds a stage with an iteration object.
  \note This version takes iteration by r-value reference.
  \note This version takes as body of the iteration an inner pipeline.
  */
  template <typename Input, typename Transformer, typename Predicate,
      template <typename T, typename P> class Iteration,
      typename ... OtherTransformers,
      requires_iteration<Iteration<Transformer,Predicate>> =0,
      requires_pipeline<Transformer> =0>
  auto add_stages(Iteration<Transformer,Predicate> && iteration_obj,
      OtherTransformers && ... other_transform_ops) 
  {
    static_assert(!is_pipeline<Transformer>, "Not implemented");
  }

  template <typename Input, typename Execution, typename Transformer, 
            template <typename, typename> class Context,
            typename ... OtherTransformers,
            requires_context<Context<Execution,Transformer>> = 0>
  auto add_stages(Context<Execution,Transformer> & context_op, 
       OtherTransformers &&... other_ops) const
  {
    return this->template add_stages<Input>(std::move(context_op),
      std::forward<OtherTransformers>(other_ops)...);
  }

  template <typename Input, typename Execution, typename Transformer, 
            template <typename, typename> class Context,
            typename ... OtherTransformers,
            requires_context<Context<Execution,Transformer>> = 0>
  auto add_stages(Context<Execution,Transformer> && context_op, 
       OtherTransformers &&... other_ops) const
  {
    static_assert(true, "Not implemented");
  }

private:

  int nworkers_;
  bool ordered_;
  std::vector<ff_node*> cleanup_stages_;

};

template <typename Generator, typename ... Transformers>
pipeline_impl::pipeline_impl(
    int nworkers, 
    bool ordered, 
    Generator && gen_op, 
    Transformers && ... transform_ops)
  :
    nworkers_{nworkers}, ordered_{ordered}
{
  using result_type = std::decay_t<typename std::result_of<Generator()>::type>;
  using generator_value_type = typename result_type::value_type;

  auto first_stage = new node_impl<void,generator_value_type,Generator>(
      std::forward<Generator>(gen_op));
  add_stage(first_stage);

  add_stages<generator_value_type>(std::forward<Transformers>(transform_ops)...);
}


} // namespace detail_ff

} // namespace grppi

#else

#endif // GRPPI_FF

#endif