#ifndef PML_RESTGOOSE_LOCK_FREE_QUEUE
#define PML_RESTGOOSE_LOCK_FREE_QUEUE

#include <atomic>
#include <memory>


namespace pml::restgoose
{
    template<typename T> class lock_free_queue
    {
        private:
            struct node
            {
                std::shared_ptr<T> data;
                node* next{nullptr};
                node()=default;
            };

            std::atomic<node*> head;
            std::atomic<node*> tail;
            
            node* pop_head()
            {
                node* const old_head=head.load();
                if(old_head==tail.load())
                {
                    return nullptr;
                }
                head.store(old_head->next);
                return old_head;
            }
            std::atomic_size_t m_nSize;

        public:
            vlock_free_queue():head(new node),tail(head.load()){}
            vlock_free_queue(const vlock_free_queue& other)=delete;
            vlock_free_queue& operator=(const vlock_free_queue& other)=delete;
            ~vlock_free_queue()
            {
                while(node* const old_head=head.load())
                {
                    head.store(old_head->next);
                    delete old_head;
                }
            }
            std::shared_ptr<T> pop()
            {
                node* old_head=pop_head();
                if(!old_head)
                {
                    return std::shared_ptr<T>();
                }
                std::shared_ptr<T> const res(old_head->data);
                delete old_head;
                --m_nSize;
                return res;
            }
            void push(T new_value)
            {
                std::shared_ptr<T> new_data(std::make_shared<T>(new_value));
                node* p=new node;
                node* const old_tail=tail.load();
                old_tail->data.swap(new_data);
                old_tail->next=p;
                tail.store(p);
                ++m_nSize;
            }
            size_t size() const { return m_nSize;}

    };
}
#endif