#ifndef DOUBLEBUFFER_H_
#define DOUBLEBUFFER_H_

#include <vector>

template<class T>
class DoubleBuffer {
protected:
	int FRONT;
	int BACK;

public:

	DoubleBuffer();
	virtual ~DoubleBuffer();
	
	//!< get current value (reference) of front buffer
	virtual T& getFront() = 0;
	//!< get current value (reference) of back buffer
	virtual T& getBack() = 0;

	//!< get current index of back buffer
	inline int getBackIdx() {return BACK;}
	//!< get current index of front buffer
	inline int getFrontIdx() {return FRONT;}
	
	//!< swap front <> back 	
	void swap(); 
};

template<class T>
class SimpleDoubleBuffer : public DoubleBuffer<T>
{
public:

	std::vector<T> m_buffer;

	SimpleDoubleBuffer();
	SimpleDoubleBuffer(T front, T back);
	virtual ~SimpleDoubleBuffer();

	T& getBack();
	T& getFront();
};

template<class T>
class ReferenceDoubleBuffer : public DoubleBuffer<T>
{
public:
	std::vector<T*> m_buffer;

	ReferenceDoubleBuffer(T* front, T* back);
	virtual ~ReferenceDoubleBuffer();
	
	T& getBack();
	T& getFront();
};
///////////////////////////////////////////////////////////////////////////////
///////// Implementation //////////////////////////////////////////////////////

//// Double Buffer ----------------------------------------------

template<class T>
DoubleBuffer<T>::DoubleBuffer()
: FRONT(1), BACK(0)
{
	
}

template<class T>
DoubleBuffer<T>::~DoubleBuffer() {
}

template<class T>
void DoubleBuffer<T>::swap()
{
	int tmp = FRONT;
	FRONT = BACK;
	BACK = tmp;
}


//// Simple Double Buffer ----------------------------------------------

template<class T>
SimpleDoubleBuffer<T>::SimpleDoubleBuffer() 
	: m_buffer(2)
{
}

template<class T>
SimpleDoubleBuffer<T>::SimpleDoubleBuffer(T front, T back) 
	: m_buffer(2)
{
	m_buffer[1] = front;
	m_buffer[0] = back;
}

template<class T>
SimpleDoubleBuffer<T>::~SimpleDoubleBuffer() {
}

template<class T>
T& SimpleDoubleBuffer<T>::getBack()
{
	return m_buffer[BACK];
}

template<class T>
T& SimpleDoubleBuffer<T>::getFront()
{
	return m_buffer[FRONT];
}

//// Reference Buffer ----------------------------------------------

template<class T>
ReferenceDoubleBuffer<T>::ReferenceDoubleBuffer(T* front, T* back) 
		: m_buffer(2)
{
	m_buffer[0] = back;
	m_buffer[1] = front;
}

template<class T>
ReferenceDoubleBuffer<T>::~ReferenceDoubleBuffer() {

}

template<class T>
T& ReferenceDoubleBuffer<T>::getBack()
{
	return *m_buffer[BACK];
}

template<class T>
T& ReferenceDoubleBuffer<T>::getFront()
{
	return *m_buffer[FRONT];
}
#endif
