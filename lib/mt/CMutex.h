#ifndef CMUTEX_H
#define CMUTEX_H

//! Mutual exclusion
/*!
A non-recursive mutual exclusion object.  Only one thread at a time can
hold a lock on a mutex.  Any thread that attempts to lock a locked mutex
will block until the mutex is unlocked.  At that time, if any threads are
blocked, exactly one waiting thread will acquire the lock and continue
running.  A thread may not lock a mutex it already owns the lock on;  if
it tries it will deadlock itself.
*/
class CMutex {
public:
	CMutex();
	//! Equivalent to default c'tor
	/*!
	Copy c'tor doesn't copy anything.  It just makes it possible to
	copy objects that contain a mutex.
	*/
	CMutex(const CMutex&);
	~CMutex();

	//! @name manipulators
	//@{

	//! Does nothing
	/*!
	This does nothing.  It just makes it possible to assign objects
	that contain a mutex.
	*/
	CMutex&				operator=(const CMutex&);

	//@}
	//! @name accessors
	//@{

	//! Lock the mutex
	/*!
	Locks the mutex, which must not have been previously locked by the
	calling thread.  This blocks if the mutex is already locked by another
	thread.

	(cancellation point)
	*/
	void				lock() const;

	//! Unlock the mutex
	/*!
	Unlocks the mutex, which must have been previously locked by the
	calling thread.
	*/
	void				unlock() const;

	//@}

private:
	void				init();
	void				fini();

private:
	friend class CCondVarBase;
	void*				m_mutex;
};

#endif