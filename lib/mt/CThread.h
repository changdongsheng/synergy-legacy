#ifndef CTHREAD_H
#define CTHREAD_H

#include "common.h"

class IJob;
class CThreadRep;

//! Thread handle
/*!
Creating a CThread creates a new context of execution (i.e. thread) that
runs simulatenously with the calling thread.  A CThread is only a handle
to a thread;  deleting a CThread does not cancel or destroy the thread it
refers to and multiple CThread objects can refer to the same thread.

Threads can terminate themselves but cannot be forced to terminate by
other threads.  However, other threads can signal a thread to terminate
itself by cancelling it.  And a thread can wait (block) on another thread
to terminate.

Most functions that can block for an arbitrary time are cancellation
points.  A cancellation point is a function that can be interrupted by
a request to cancel the thread.  Cancellation points are noted in the
documentation.
*/
// note -- do not derive from this class
class CThread {
public:
	//! Run \c adoptedJob in a new thread
	/*!
	Create and start a new thread executing the \c adoptedJob.  The
	user data can be retrieved with getUserData().  The new thread
	takes ownership of \c adoptedJob and will delete it.
	*/
	CThread(IJob* adoptedJob, void* userData = 0);

	//! Duplicate a thread handle
	/*!
	Make a new thread object that refers to an existing thread.
	This does \b not start a new thread.
	*/
	CThread(const CThread&);

	//! Release a thread handle
	/*!
	Release a thread handle.  This does not terminate the thread.  A thread
	will keep running until the job completes or calls exit() or allows
	itself to be cancelled.
	*/
	~CThread();

	//! @name manipulators
	//@{

	//! Assign thread handle
	/*!
	Assign a thread handle.  This has no effect on the threads, it simply
	makes this thread object refer to another thread.  It does \b not
	start a new thread.
	*/
	CThread&			operator=(const CThread&);

	//! Initialize the thread library
	/*!
	Initialize the thread library.  This \b must be called before
	any other thread methods or creating a thread object.  It is
	harmless to call init() multiple times.
	*/
	static void			init();

	//! Sleep
	/*!
	Blocks the calling thread for \c timeout seconds.  If
	\c timeout < 0.0 then the call returns immediately.  If \c timeout
	== 0.0 then the calling thread yields the CPU.

	(cancellation point)
	*/
	static void			sleep(double timeout);

	//! Terminate the calling thread
	/*!
	Terminate the calling thread.  This function does not return but
	the stack is unwound and automatic objects are destroyed, as if
	exit() threw an exception (which is, in fact, what it does).  The
	argument is saved as the result returned by getResult().  If you
	have \c catch(...) blocks then you should add the following before
	each to avoid catching the exit:
	\code
	catch(CThreadExit&) { throw; }
	\endcode
	or add the \c RETHROW_XTHREAD macro to the \c catch(...) block.
	*/
	static void			exit(void*);

	//! Enable or disable cancellation
	/*!
	Enable or disable cancellation.  The default is enabled.  This is not
	a cancellation point so if you just enabled cancellation and want to
	allow immediate cancellation you need to call testCancel().
	Returns the previous state.
	*/
	static bool			enableCancel(bool);

	//! Cancel thread
	/*!
	Cancel the thread.  cancel() never waits for the thread to
	terminate;  it just posts the cancel and returns.  A thread will
	terminate when it enters a cancellation point with cancellation
	enabled.  If cancellation is disabled then the cancel is
	remembered but not acted on until the first call to a
	cancellation point after cancellation is enabled.
	
	A cancellation point is a function that can act on cancellation.
	A cancellation point does not return if there's a cancel pending.
	Instead, it unwinds the stack and destroys automatic objects, as
	if cancel() threw an exception (which is, in fact, what it does).
	Threads must take care to unlock and clean up any resources they
	may have, especially mutexes.  They can \c catch(XThreadCancel) to
	do that then rethrow the exception or they can let it happen
	automatically by doing clean up in the d'tors of automatic
	objects (like CLock).  Clients are strongly encouraged to do the latter.
	During cancellation, further cancel() calls are ignored (i.e.
	a thread cannot be interrupted by a cancel during cancellation).
	
	Clients that \c catch(XThreadCancel) must always rethrow the
	exception.  Clients that \c catch(...) must either rethrow the
	exception or include a \c catch(XThreadCancel) handler that
	rethrows.  The \c RETHROW_XTHREAD macro may be useful for that.
	*/
	void				cancel();

	//! Change thread priority
	/*!
	Change the priority of the thread.  Normal priority is 0, 1 is
	the next lower, etc.  -1 is the next higher, etc. but boosting
	the priority may not be permitted and will be silenty ignored.
	*/
	void				setPriority(int n);

	//@}
	//! @name accessors
	//@{

	//! Get current thread's handle
	/*!
	Return a CThread object representing the calling thread.
	*/
	static CThread		getCurrentThread();

	//! Test for cancellation
	/*!
	testCancel() does nothing but is a cancellation point.  Call
	this to make a function itself a cancellation point.  If the
	thread was cancelled and cancellation is enabled this will
	cause the thread to unwind the stack and terminate.

	(cancellation point)
	*/
	static void			testCancel();

	//! Get the thread user data
	/*!
	Gets the user data passed to the c'tor that created this thread.
	*/
	void*				getUserData();

	//! Wait for thread to terminate
	/*!
	Waits for the thread to terminate (by exit() or cancel() or
	by returning from the thread job) for up to \c timeout seconds,
	returning true if the thread terminated and false otherwise.
	This returns immediately with false if called by a thread on
	itself and immediately with true if the thread has already
	terminated.  This will wait forever if \c timeout < 0.0.

	(cancellation point)
	*/
	bool				wait(double timeout = -1.0) const;

#if WINDOWS_LIKE
	//! Wait for an event (win32)
	/*!
	Wait for the message queue to contain a message for up to \c timeout
	seconds.  This returns immediately if any message is available
	(including messages that were already in the queue during the last
	call to \c GetMessage() or \c PeekMessage() or waitForEvent().
	Returns true iff a message is available.  This will wait forever
	if \c timeout < 0.0.

	This method is available under win32 only.

	(cancellation point)
	*/
	static bool			waitForEvent(double timeout = -1.0);
#endif

	//! Get the exit result
	/*!
	Returns the exit result.  This does an implicit wait().  It returns
	NULL immediately if called by a thread on itself or on a thread that
	was cancelled.

	(cancellation point)
	*/
	void*				getResult() const;

	//! Compare thread handles
	/*!
	Returns true if two CThread objects refer to the same thread.
	*/
	bool				operator==(const CThread&) const;

	//! Compare thread handles
	/*!
	Returns true if two CThread objects do not refer to the same thread.
	*/
	bool				operator!=(const CThread&) const;

	//@}

private:
	CThread(CThreadRep*);

private:
	CThreadRep*			m_rep;
};

//! Disable cancellation utility
/*!
This class disables cancellation for the current thread in the c'tor
and enables it in the d'tor.
*/
class CThreadMaskCancel {
public:
	CThreadMaskCancel();
	~CThreadMaskCancel();

private:
	bool				m_old;
};

#endif