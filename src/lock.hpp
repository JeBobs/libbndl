#include <mutex>

class Lock
{
public:
	Lock(std::mutex &m)
		: m_mutex(m)
	{
		m_mutex.lock();
	}
	~Lock()
	{
		m_mutex.unlock();
	}

private:
	std::mutex &m_mutex;
};
