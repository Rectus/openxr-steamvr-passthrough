
#pragma once

template<typename T> class FrameQueue;

namespace
{
	template<typename T> struct QueueEntry
	{
		std::shared_ptr<T> Frame;
		std::atomic<int> NumReaders = 0;
	};
}


template<typename T> class FramePtr
{
public:
	FramePtr()
		: m_queue(nullptr)
		, m_bIsWrite(false)
	{

	}
	FramePtr(FrameQueue<T>* queue, std::shared_ptr<QueueEntry<T>> entry, bool bIsWrite)
		: m_queue(queue)
		, m_entry(entry)
		, m_bIsWrite(bIsWrite)
	{

	}

	FramePtr(FramePtr&& other) noexcept
	{
		m_queue = other.m_queue;
		m_entry = other.m_entry;
		m_bIsWrite = false;
	}

	FramePtr(const FramePtr& other) = delete;

	~FramePtr()
	{
		if (m_queue && m_entry.get())
		{
			m_bIsWrite ? m_queue->RescindWrite(m_entry) : m_queue->ReleaseRead(m_entry->Frame);
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
		return m_entry.get() != nullptr;
	}

	std::shared_ptr<T> GetSharedPointer()
	{
		if (m_entry.get())
		{
			return m_entry->Frame;
		}
		return std::shared_ptr<T>();
	}

	void CommitWrite()
	{
		if (m_entry.get() && m_bIsWrite)
		{
			m_queue->CommitWrite(m_entry);
			m_queue = nullptr;
			m_entry.reset();
		}
	}

	bool CommitWriteAndAcquireRead()
	{
		if (m_entry.get() && m_bIsWrite)
		{
			m_bIsWrite = false;
			m_queue->CommitWriteAndAcquireRead(m_entry);
			return true;
		}
		return false;
	}

	// Invalidates the FramePtr. Requires manually releasing the read from the queue.
	std::shared_ptr<T> AcquireManualRead()
	{
		if (!m_entry.get())
		{
			return std::shared_ptr<T>();
		}

		std::shared_ptr<T> frame = m_entry->Frame;
		m_queue = nullptr;
		m_entry.reset();

		return frame;
	}

private:
	FrameQueue<T>* m_queue;
	std::shared_ptr<QueueEntry<T>> m_entry;
	bool m_bIsWrite;
};




template<typename T> class FrameQueue
{
public:
	FrameQueue(int numFrames)
	{
		m_idleEntries.reserve(numFrames);
		m_readEntries.reserve(numFrames);

		for (int i = 0; i < numFrames; i++)
		{
			auto entry = std::make_shared<QueueEntry<T>>();
			entry->Frame = std::make_shared<T>();
			m_idleEntries.push_back(entry);
		}
	}

	FramePtr<T> AcquireRead()
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		if (m_readEntries.empty())
		{
			return FramePtr<T>();
		}

		m_readEntries.back()->NumReaders++;
		return FramePtr<T>(this, m_readEntries.back(), false);
	}

	void ReleaseRead(std::shared_ptr<T> frame)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		for (int i = 0; i < (int)m_readEntries.size(); i++)
		{
			if (m_readEntries[i]->Frame.get() == frame.get())
			{
				m_readEntries[i]->NumReaders--;

				// Return to idle queue if no one else is reading
				if (m_readEntries.size() > 1 && m_readEntries[i]->NumReaders <= 0)
				{
					m_idleEntries.push_back(m_readEntries[i]);
					m_readEntries.erase(m_readEntries.begin() + i);
				}

				return;
			}
		}	
	}

	FramePtr<T> AcquireWrite()
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		if (m_idleEntries.size() > 0)
		{
			auto entry = m_idleEntries.back();
			m_idleEntries.pop_back();

			return FramePtr<T>(this, entry, true);
		}
		return FramePtr<T>();
	}

	void CommitWrite(std::shared_ptr<QueueEntry<T>> frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		// Remove any stale frames not being read
		for (int i = (int)m_readEntries.size() - 1; i >= 0; i--)
		{
			if (m_readEntries[i]->NumReaders <= 0)
			{
				m_idleEntries.push_back(m_readEntries[i]);
				m_readEntries.erase(m_readEntries.begin() + i);
			}
		}
		
		m_readEntries.push_back(frameEntry);
	}

	bool CommitWriteAndAcquireRead(std::shared_ptr<QueueEntry<T>> frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		// Remove any stale frames not being read
		for (int i = (int)m_readEntries.size() - 1; i >= 0; i--)
		{
			if (m_readEntries[i]->NumReaders <= 0)
			{
				m_idleEntries.push_back(m_readEntries[i]);
				m_readEntries.erase(m_readEntries.begin() + i);
			}
		}

		m_readEntries.push_back(frameEntry);
		frameEntry->NumReaders++;

		return true;
	}

	void RescindWrite(std::shared_ptr<QueueEntry<T>> frameEntry)
	{
		std::lock_guard<std::mutex> accessLock(m_accessMutex);

		m_idleEntries.push_back(frameEntry);
	}

private:
	std::vector<std::shared_ptr<QueueEntry<T>>> m_idleEntries;
	std::vector<std::shared_ptr<QueueEntry<T>>> m_readEntries;
	std::mutex m_accessMutex;
};

