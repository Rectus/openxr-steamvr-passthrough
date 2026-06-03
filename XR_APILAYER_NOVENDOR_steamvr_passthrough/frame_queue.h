
#pragma once

template<typename T> class FrameQueue;

namespace
{
	template<typename T> struct QueueEntry
	{
		std::shared_ptr<T> Frame;
		std::shared_mutex Mutex;
	};
}


template<typename T> class FramePtr
{
public:
	FramePtr()
		: m_queue(nullptr)
		, m_entry(nullptr)
		, m_bIsWrite(false)
	{

	}
	FramePtr(FrameQueue<T>* queue, QueueEntry<T>* entry, bool bIsWrite)
		: m_queue(queue)
		, m_entry(entry)
		, m_bIsWrite(bIsWrite)
	{

	}

	FramePtr(FramePtr&& other)
	{
		m_queue = other.m_queue;
		m_entry = other.m_entry;
	}

	FramePtr(const FramePtr& other) = delete;

	~FramePtr()
	{
		if (m_entry)
		{
			m_bIsWrite ? m_queue->RescindWrite(m_entry) : m_queue->ReleaseRead(m_entry);
		}
	}

	FramePtr& operator= (const FramePtr&) = delete;

	T& operator* ()
	{
		return *m_entry->Frame.get();
	}

	T* operator-> ()
	{
		return m_entry->Frame.get();
	}

	bool HasFrame()
	{
		return m_entry != nullptr;
	}

	std::shared_ptr<T> GetSharedPointer()
	{
		if (m_entry)
		{
			return m_entry->Frame;
		}
		return std::shared_ptr<T>();
	}

	void CommitWrite()
	{
		if (m_entry && m_bIsWrite)
		{
			m_queue->CommitWrite(m_entry);
			m_queue = nullptr;
			m_entry = nullptr;
		}
	}

	bool CommitWriteAndAcuireRead()
	{
		if (m_entry && m_bIsWrite)
		{
			m_queue->CommitWrite(m_entry);
			
			m_bIsWrite = false;
			if (m_queue->AcquireReadFromWrite(m_entry))
			{
				return true;
			}
			m_queue = nullptr;
			m_entry = nullptr;	
		}
		return false;
	}

private:
	FrameQueue<T>* m_queue;
	QueueEntry<T>* m_entry;
	bool m_bIsWrite;
};




template<typename T> class FrameQueue
{
public:
	FrameQueue(int numFrames)
		: m_entries(numFrames)
	{
		for (auto& entry : m_entries)
		{
			entry.Frame = std::make_shared<T>();
		}
	}

	FramePtr<T> AcquireRead()
	{
		if (m_newestFrame < 0)
		{
			return FramePtr<T>();
		}
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		if (m_entries[m_newestFrame].Mutex.try_lock_shared())
		{
			m_lastReadFrame = m_newestFrame;

			return FramePtr<T>(this, &m_entries[m_newestFrame], false);
		}

		return FramePtr<T>();
	}

	bool AcquireReadFromWrite(QueueEntry<T>* frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		return frameEntry->Mutex.try_lock_shared();
	}

	void ReleaseRead(QueueEntry<T>* frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		frameEntry->Mutex.unlock_shared();
	}

	FramePtr<T> AcquireWrite()
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		int id = m_newestFrame < 0 ? 0 : (m_newestFrame + 1) % m_entries.size();

		if (m_entries[id].Mutex.try_lock())
		{
			return FramePtr<T>(this, &m_entries[id], true);
		}
		return FramePtr<T>();
	}

	void CommitWrite(QueueEntry<T>* frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);
		
		frameEntry->Mutex.unlock();

		// Keep swapping the last two written frames if we are producing them faster than reading them.
		// We can't easily detect when the GPU has finised accessing the frame, 
		// this should keep them from being overwritten while in use with 3 + 2 frames in the queue.
		if (m_lastReadFrame != m_newestFrame)
		{
			frameEntry->Frame.swap(m_entries[m_newestFrame].Frame);
		}
		else
		{
			for (int i = 0; i < m_entries.size(); i++)
			{
				if (&m_entries[i] == frameEntry)
				{
					m_newestFrame = i;
					break;
				}
			}
		}
	}

	void RescindWrite(QueueEntry<T>* frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		frameEntry->Mutex.unlock();
	}

private:
	std::vector<QueueEntry<T>> m_entries;
	std::mutex m_accessMutex;
	int m_newestFrame = -1;
	int m_lastReadFrame = -1;
};

