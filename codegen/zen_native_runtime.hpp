#ifndef ZEN_NATIVE_RUNTIME_HPP
#define ZEN_NATIVE_RUNTIME_HPP

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace zen_native
{
    template <class T, std::size_t N>
    class StaticArray
    {
    public:
        StaticArray() : values_() {}
        T &operator[](std::size_t index) { return values_[index]; }
        const T &operator[](std::size_t index) const { return values_[index]; }

    private:
        T values_[N];
    };

    template <class T>
    class DynArray
    {
    public:
        DynArray() : data_(), sizes_() {}
        void dim(const int64_t *dims, int n)
        {
            sizes_.assign(dims, dims + n);
            int64_t total = 1;
            for (int i = 0; i < n; ++i) total *= sizes_[static_cast<std::size_t>(i)];
            data_.assign(static_cast<std::size_t>(total), T());
        }
        T &at(int64_t index)
        {
            check_bounds(index);
            return data_[static_cast<std::size_t>(index)];
        }
        const T &at(int64_t index) const
        {
            check_bounds(index);
            return data_[static_cast<std::size_t>(index)];
        }
        void check_bounds(int64_t index) const
        {
            if (index < 0 || static_cast<std::size_t>(index) >= data_.size())
            {
                std::fflush(stdout);
                std::fprintf(stderr, "[runtime error] Array index out of bounds\n");
                std::exit(1);
            }
        }
        int64_t dim_size(int dim) const { return sizes_[static_cast<std::size_t>(dim)]; }

    private:
        std::vector<T> data_;
        std::vector<int64_t> sizes_;
    };
    class Heap;

    class Object
    {
    public:
        Object() : marked_(false), previous_(0), next_(0), alive_(true), handle_(0) {}
        virtual ~Object() {}
        virtual void trace(Heap &) {}
        bool alive() const { return alive_; }

    private:
        bool marked_;
        Object *previous_;
        Object *next_;
        bool alive_;
        long long handle_;
        friend class Heap;
        template <class T> friend class TypeList;
    };

    class Heap
    {
    public:
        Heap() : next_handle_(0) {}
        ~Heap()
        {
            for (std::size_t i = 0; i < objects_.size(); ++i)
                delete objects_[i];
        }

        Heap(const Heap &) = delete;
        Heap &operator=(const Heap &) = delete;

        template <class T, class... Args>
        T *allocate(Args &&...args)
        {
            T *object = new T(std::forward<Args>(args)...);
            objects_.push_back(object);
            return object;
        }

        void push_root(Object **root) { roots_.push_back(root); }

        void pop_root(Object **root)
        {
            if (!roots_.empty() && roots_.back() == root)
            {
                roots_.pop_back();
                return;
            }
            roots_.erase(std::remove(roots_.begin(), roots_.end(), root), roots_.end());
        }

        void mark(Object *object)
        {
            if (!object || object->marked_) return;
            object->marked_ = true;
            object->trace(*this);
        }

        void collect()
        {
            for (std::size_t i = 0; i < roots_.size(); ++i)
                mark(*roots_[i]);
            std::vector<Object *> survivors;
            survivors.reserve(objects_.size());
            for (std::size_t i = 0; i < objects_.size(); ++i)
            {
                Object *object = objects_[i];
                if (object->marked_)
                {
                    object->marked_ = false;
                    survivors.push_back(object);
                }
                else
                    delete object;
            }
            objects_.swap(survivors);
        }
        long long handle(Object *object)
        {
            if (!object || !object->alive_) return 0;
            if (!object->handle_) { object->handle_ = ++next_handle_; handles_.push_back(object); }
            return object->handle_;
        }
        /* Blitz's Object: the handle's object when it really is of that
           Type, Null otherwise - a handle taken from one Type and read
           back as another is Null, not a reinterpreted pointer (the VM's
           nat_object compares the struct definition for the same reason).
           dynamic_cast is what does the comparing here; Object is
           polymorphic, so it costs nothing extra to make it safe. */
        template <class T> T *object(long long handle) const
        {
            if (handle <= 0 || static_cast<std::size_t>(handle) > handles_.size()) return 0;
            Object *value = handles_[static_cast<std::size_t>(handle - 1)];
            return value && value->alive_ ? dynamic_cast<T *>(value) : 0;
        }

    private:
        std::vector<Object *> objects_;
        std::vector<Object **> roots_;
        std::vector<Object *> handles_;
        long long next_handle_;
    };

    template <class T>
    class TypeList
    {
    public:
        explicit TypeList(Heap &heap) : heap_(&heap), first_(0), last_(0)
        {
            heap_->push_root(reinterpret_cast<Object **>(&first_));
        }
        ~TypeList()
        {
            heap_->pop_root(reinterpret_cast<Object **>(&first_));
        }

        TypeList(const TypeList &) = delete;
        TypeList &operator=(const TypeList &) = delete;

        T *first() const { return first_; }
        T *last() const { return last_; }

        T *after(T *object) const
        {
            return object ? static_cast<T *>(object->next_) : 0;
        }

        T *before(T *object) const
        {
            return object ? static_cast<T *>(object->previous_) : 0;
        }

        void append(T *object)
        {
            object->previous_ = last_;
            object->next_ = 0;
            object->alive_ = true;
            if (last_) last_->next_ = object;
            else first_ = object;
            last_ = object;
        }

        void erase(T *object)
        {
            if (!object || !object->alive_) return;
            Object *previous = object->previous_;
            Object *next = object->next_;
            if (previous) previous->next_ = next;
            else first_ = static_cast<T *>(next);
            if (next) next->previous_ = previous;
            else last_ = static_cast<T *>(previous);
            object->alive_ = false;
        }

        void insert_before(T *object, T *target)
        {
            if (!object || !target || !object->alive_ || !target->alive_ || object == target) return;
            erase(object);
            Object *previous = target->previous_;
            object->previous_ = previous;
            object->next_ = target;
            if (previous) previous->next_ = object;
            else first_ = object;
            target->previous_ = object;
            object->alive_ = true;
        }

        void insert_after(T *object, T *target)
        {
            if (!object || !target || !object->alive_ || !target->alive_ || object == target) return;
            erase(object);
            Object *next = target->next_;
            object->previous_ = target;
            object->next_ = next;
            target->next_ = object;
            if (next) next->previous_ = object;
            else last_ = object;
            object->alive_ = true;
        }

        void clear()
        {
            T *object = first_;
            while (object)
            {
                T *next = static_cast<T *>(object->next_);
                object->previous_ = 0;
                object->next_ = 0;
                object->alive_ = false;
                object = next;
            }
            first_ = 0;
            last_ = 0;
        }

    private:
        Heap *heap_;
        T *first_;
        T *last_;
    };

    inline bool object_equal(const Object *left, const Object *right)
    {
        const Object *live_left = left && left->alive() ? left : 0;
        const Object *live_right = right && right->alive() ? right : 0;
        return live_left == live_right;
    }
    template <class T>
    class Root
    {
    public:
        Root(Heap &heap, T *object = 0) : heap_(&heap), object_(object)
        {
            heap_->push_root(reinterpret_cast<Object **>(&object_));
        }
        Root(const Root &other) : heap_(other.heap_), object_(other.object_)
        {
            heap_->push_root(reinterpret_cast<Object **>(&object_));
        }
        ~Root() { heap_->pop_root(reinterpret_cast<Object **>(&object_)); }

        Root &operator=(T *object)
        {
            object_ = object;
            return *this;
        }
        T *get() const { return object_; }
        operator T *() const { return object_; }

    private:
        Heap *heap_;
        T *object_;
    };
}

#endif
