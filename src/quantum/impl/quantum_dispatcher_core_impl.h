/*
** Copyright 2018 Bloomberg Finance L.P.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
//NOTE: DO NOT INCLUDE DIRECTLY

//##############################################################################################
//#################################### IMPLEMENTATIONS #########################################
//##############################################################################################

namespace Bloomberg {
namespace quantum {

inline
DispatcherCore::DispatcherCore(int numCoroutineThreads,
                               int numIoThreads,
                               bool pinCoroutineThreadsToCores) :
    _coroQueues((numCoroutineThreads == -1) ? std::thread::hardware_concurrency() : numCoroutineThreads),
    _sharedIoQueue(nullptr),
    _ioQueues(numIoThreads, IoQueue(&_sharedIoQueue)),
    _terminated(ATOMIC_FLAG_INIT)
{
    if (pinCoroutineThreadsToCores)
    {
        if (_coroQueues.size() > std::thread::hardware_concurrency())
        {
            throw std::runtime_error("Number of queues exceeds cores.");
        }
        for (size_t i = 0; i < _coroQueues.size(); ++i)
        {
            _coroQueues[i].pinToCore(i);
        }
    }
}

inline
DispatcherCore::~DispatcherCore()
{
    terminate();
}

inline
void DispatcherCore::terminate()
{
    if (!_terminated.test_and_set())
    {
        for (auto&& queue : _coroQueues)
        {
            queue.terminate();
        }
        for (auto&& queue : _ioQueues)
        {
            queue.terminate();
        }
        _sharedIoQueue.terminate();
    }
}

inline
size_t DispatcherCore::size(IQueue::QueueType type,
                            int queueId) const
{
    if (type == IQueue::QueueType::All)
    {
        if (queueId != (int)IQueue::QueueId::All)
        {
            throw std::runtime_error("Cannot specify queue id");
        }
        return coroSize((int)IQueue::QueueId::All) + ioSize((int)IQueue::QueueId::All);
    }
    else if (type == IQueue::QueueType::Coro)
    {
        return coroSize(queueId);
    }
    return ioSize(queueId);
}

inline
bool DispatcherCore::empty(IQueue::QueueType type,
                           int queueId) const
{
    if (type == IQueue::QueueType::All)
    {
        if (queueId != (int)IQueue::QueueId::All)
        {
            throw std::runtime_error("Cannot specify queue id");
        }
        return coroEmpty((int)IQueue::QueueId::All) && ioEmpty((int)IQueue::QueueId::All);
    }
    else if (type == IQueue::QueueType::Coro)
    {
        return coroEmpty(queueId);
    }
    return ioEmpty(queueId);
}

inline
size_t DispatcherCore::coroSize(int queueId) const
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        size_t size = 0;
        for (auto&& queue : _coroQueues)
        {
            size += queue.size();
        }
        return size;
    }
    return _coroQueues.at(queueId).size();
}

inline
bool DispatcherCore::coroEmpty(int queueId) const
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        for (auto&& queue : _coroQueues)
        {
            if (!queue.empty()) return false;
        }
        return true;
    }
    return _coroQueues.at(queueId).empty();
}

inline
size_t DispatcherCore::ioSize(int queueId) const
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        size_t size = 0;
        for (auto&& queue : _ioQueues)
        {
            size += queue.size();
        }
        size += _sharedIoQueue.size();
        return size;
    }
    else if (queueId == (int)IQueue::QueueId::Any)
    {
        return _sharedIoQueue.size();
    }
    return _ioQueues.at(queueId).size();
}

inline
bool DispatcherCore::ioEmpty(int queueId) const
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        for (auto&& queue : _ioQueues)
        {
            if (!queue.empty()) return false;
        }
        return _sharedIoQueue.empty();
    }
    else if (queueId == (int)IQueue::QueueId::Any)
    {
        return _sharedIoQueue.empty();
    }
    return _ioQueues.at(queueId).empty();
}

inline
QueueStatistics DispatcherCore::stats(IQueue::QueueType type, int queueId)
{
    if (type == IQueue::QueueType::All)
    {
        if (queueId != (int)IQueue::QueueId::All)
        {
            throw std::runtime_error("Cannot specify queue id");
        }
        return coroStats((int)IQueue::QueueId::All) + ioStats((int)IQueue::QueueId::All);
    }
    else if (type == IQueue::QueueType::Coro)
    {
        return coroStats(queueId);
    }
    return ioStats(queueId);
}

inline
QueueStatistics DispatcherCore::coroStats(int queueId)
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        QueueStatistics stats;
        for (auto&& queue : _coroQueues)
        {
            stats += queue.stats();
        }
        return stats;
    }
    else
    {
        if ((queueId >= (int)_coroQueues.size()) || (queueId < 0))
        {
            throw std::runtime_error("Invalid queue id");
        }
        return static_cast<const QueueStatistics&>(_coroQueues[queueId].stats());
    }
}

inline
QueueStatistics DispatcherCore::ioStats(int queueId)
{
    if (queueId == (int)IQueue::QueueId::All)
    {
        QueueStatistics stats;
        for (auto&& queue : _ioQueues)
        {
            stats += queue.stats();
        }
        stats += _sharedIoQueue.stats();
        return stats;
    }
    else if (queueId == (int)IQueue::QueueId::Any)
    {
        return static_cast<const QueueStatistics&>(_sharedIoQueue.stats());
    }
    else
    {
        if ((queueId >= (int)_coroQueues.size()) || (queueId < 0))
        {
            throw std::runtime_error("Invalid queue id");
        }
        return static_cast<const QueueStatistics&>(_ioQueues[queueId].stats());
    }
}

inline
void DispatcherCore::resetStats()
{
    for (auto&& queue : _coroQueues)
    {
        queue.stats().reset();
    }
    _sharedIoQueue.stats().reset();
    for (auto&& queue : _ioQueues)
    {
        queue.stats().reset();
    }
}

inline
void DispatcherCore::post(Task::ptr task)
{
    if (!task)
    {
        return;
    }
    
    if (task->getQueueId() == (int)IQueue::QueueId::Any)
    {
        size_t index = 0;
        
        //Insert into the shortest queue or the first empty queue found
        size_t numTasks = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < _coroQueues.size(); ++i)
        {
            size_t queueSize = _coroQueues[i].size();
            if (queueSize < numTasks)
            {
                numTasks = queueSize;
                index = i;
            }
            if (numTasks == 0)
            {
                break; //reached an empty queue
            }
        }
        
        task->setQueueId(index); //overwrite the queueId with the selected one
    }
    else
    {
        if (task->getQueueId() >= (int)_coroQueues.size())
        {
            throw std::runtime_error("Queue id out of bounds");
        }
    }
    
    _coroQueues[task->getQueueId()].enqueue(task);
    
}

inline
void DispatcherCore::postAsyncIo(IoTask::ptr task)
{
    if (!task)
    {
        return;
    }
    
    if (task->getQueueId() == (int)IQueue::QueueId::Any)
    {
        //insert the task into the main queue
        _sharedIoQueue.enqueue(task);
        
        //Signal all threads there is work to do
        for (auto&& queue : _ioQueues)
        {
            queue.signalEmptyCondition(false);
        }
    }
    else
    {
        if (task->getQueueId() >= (int)_ioQueues.size())
        {
            throw std::runtime_error("Queue id out of bounds");
        }
        
        //Run on specific queue
        _ioQueues[task->getQueueId()].enqueue(task);
    }
}

}}