#pragma once

#include <boost/container/static_vector.hpp>
#include <cassert>
#include <memory>
#include <stack>

namespace polysynth
{
/**
 * Object pool using smart pointers, such that acquiring objects from the pool follows the RAII
 * principle, enforced by std smart pointers. When an acquired object is deleted,
 * it automatically returns to the pool it was acquired from.
 * Note that T class destructor is not called when the object returns to the pool,
 * neither is constructor called when the object is taken from the pool.
 * So any re-initialization has to be done explicitely after obtaining
 * the object from the pool.
 *
 * Taken from https://swarminglogic.com/jotting/2015_05_smartpool
 */
template<class T, size_t maxCapacity, class D = std::default_delete<T>>
class ObjectPool
{
  private:
    struct ReturnToPool_Deleter
    {
        explicit ReturnToPool_Deleter(std::weak_ptr<ObjectPool<T, maxCapacity, D>*> pool)
          : pool_(std::move(pool))
        {
        }

        void operator()(T* ptr)
        {
            assert(ptr);
            if (auto pool_ptr = pool_.lock())
            {
                (*pool_ptr.get())->add(std::unique_ptr<T, D>{ ptr });
            }
            else
            {
                D{}(ptr);
            }
        }

      private:
        std::weak_ptr<ObjectPool<T, maxCapacity, D>*> pool_;
    };

  public:
    using ptr_type = std::unique_ptr<T, ReturnToPool_Deleter>;

    ObjectPool()
      : this_ptr_(new ObjectPool<T, maxCapacity, D>*(this))
    {
    }

    void add(std::unique_ptr<T, D> t) { pool_.push_back(std::move(t)); }

    ptr_type acquire()
    {
        assert(!pool_.empty());

        ptr_type tmp(
          pool_.back().release(),
          ReturnToPool_Deleter{ std::weak_ptr<ObjectPool<T, maxCapacity, D>*>{ this_ptr_ } });
        pool_.pop_back();
        return tmp;
    }

    [[nodiscard]] ptr_type createNullPtr()
    {
        return ptr_type(nullptr, ReturnToPool_Deleter{ this_ptr_ });
    }

    [[nodiscard]] bool empty() const { return pool_.empty(); }

    [[nodiscard]] size_t size() const { return pool_.size(); }

  private:
    std::shared_ptr<ObjectPool<T, maxCapacity, D>*> this_ptr_;
    boost::container::static_vector<std::unique_ptr<T, D>, maxCapacity> pool_;
};

template<typename T>
struct AssertDeleter
{
    void operator()(T* /* ptr */) const
    {
#ifndef UNIT_TESTS
        assert(false);
#endif
    };
};

}