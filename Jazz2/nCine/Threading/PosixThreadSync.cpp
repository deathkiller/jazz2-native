//#include "ThreadSync.h"
//
//namespace nCine {
//
/////////////////////////////////////////////////////////////
//// Mutex CLASS
/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////
//// CONSTRUCTORS and DESTRUCTOR
/////////////////////////////////////////////////////////////
//
//Mutex::Mutex()
//{
//	pthread_mutex_init(&mutex_, nullptr);
//}
//
//Mutex::~Mutex()
//{
//	pthread_mutex_destroy(&mutex_);
//}
//
/////////////////////////////////////////////////////////////
//// PUBLIC FUNCTIONS
/////////////////////////////////////////////////////////////
//
//void Mutex::lock()
//{
//	pthread_mutex_lock(&mutex_);
//}
//
//void Mutex::unlock()
//{
//	pthread_mutex_unlock(&mutex_);
//}
//
//int Mutex::tryLock()
//{
//	return pthread_mutex_trylock(&mutex_);
//}
//
/////////////////////////////////////////////////////////////
//// CondVariable CLASS
/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////
//// CONSTRUCTORS and DESTRUCTOR
/////////////////////////////////////////////////////////////
//
//CondVariable::CondVariable()
//{
//	pthread_cond_init(&cond_, nullptr);
//}
//
//CondVariable::~CondVariable()
//{
//	pthread_cond_destroy(&cond_);
//}
//
/////////////////////////////////////////////////////////////
//// PUBLIC FUNCTIONS
/////////////////////////////////////////////////////////////
//
//void CondVariable::wait(Mutex &mutex)
//{
//	pthread_cond_wait(&cond_, &(mutex.mutex_));
//}
//
//void CondVariable::signal()
//{
//	pthread_cond_signal(&cond_);
//}
//
//void CondVariable::broadcast()
//{
//	pthread_cond_broadcast(&cond_);
//}
//
/////////////////////////////////////////////////////////////
//// RWLock CLASS
/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////
//// CONSTRUCTORS and DESTRUCTOR
/////////////////////////////////////////////////////////////
//
//RWLock::RWLock()
//{
//	pthread_rwlock_init(&rwlock_, nullptr);
//}
//
//RWLock::~RWLock()
//{
//	pthread_rwlock_destroy(&rwlock_);
//}
//
/////////////////////////////////////////////////////////////
//// Barrier CLASS
/////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////
//// CONSTRUCTORS and DESTRUCTOR
/////////////////////////////////////////////////////////////
//
//#if !defined(__ANDROID__) && !defined(__APPLE__)
//
//Barrier::Barrier(unsigned int count)
//{
//	pthread_barrier_init(&barrier_, nullptr, count);
//}
//
//Barrier::~Barrier()
//{
//	pthread_barrier_destroy(&barrier_);
//}
//
//#endif
//
//}
