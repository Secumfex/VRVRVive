#ifndef TIMER_H
#define TIMER_H

#include <vector>
#include <string>
#include <unordered_map>

#include "Updatable.h"

class Timer : public Updatable
{
protected:
	std::vector< double> m_savedTime;	// vector of saved values
	double m_elapsedTime;	// elapsed time since timer started
	bool   m_running;		// true if timer is running
public:
	Timer(bool running);
	~Timer();
	virtual void update(float d_t);	// add d_t to elapsed time
	virtual void reset();			// sets elapsed Time to 0
	virtual void toggleRunning();	// toggles running to true or false

	void clearSavedTimes();	// clears the vector of saved times
	void saveCurrentElapsedTime();	// pushes the current elapsed time into vector

	double getElapsedTime() const;
	double* getElapsedTimePtr();
	bool isRunning() const;
	bool* getRunningPtr();
	void setRunning(bool running);
	const std::vector<double>& getSavedTime() const;
};

class GLFWTimer : public Timer
{
protected:
	double m_lastTime;
public:
	GLFWTimer(bool running = false);
	~GLFWTimer();
	virtual void update(float d_t);	// read glfwtime and add actual difference to elapsed time if running
	virtual void toggleRunning();
};

class OpenGLTimer
{
protected:
	unsigned int m_queryID[2];
	unsigned long long m_startTime;
	unsigned long long m_stopTime;
	double m_executionTime;
public:
	OpenGLTimer(bool running = true);
	~OpenGLTimer();
	virtual void start();
	virtual void stop();
	virtual double getTime();
	virtual void reset();
};

class Timings 
{
protected:
	std::unordered_map<std::string, OpenGLTimer> m_timers;
public:
	std::unordered_map<std::string, double> m_lastTimings;
	void beginTimer(const std::string& timer);
	void stopTimer(const std::string& timer);
	void resetTimer(const std::string& timer);
	void deleteTimer(const std::string& timer);
	OpenGLTimer* getTimerPtr(const std::string& timer);
};

class OpenGLTimings
{
public:
	struct Timer {
		unsigned int queryID[2];
		unsigned long long startTime;
		unsigned long long stopTime;
		double lastTime; // start time
		double lastTiming;
		Timer(){queryID[0] = -1;queryID[1] = -1; lastTime=0.0; lastTiming=0.0;}
	};
	
	struct Timestamp {
		unsigned int queryID;
		unsigned long long timestamp;
		double lastTime;
		Timestamp(){queryID = -1; lastTime=0.0;}
	};

	struct TimerElapsed {
		unsigned int queryID[2];
		unsigned long long startTime;
		unsigned long long elapsedTime;
		double lastTime; // start time
		double lastTiming; // elapsed time
		TimerElapsed(){queryID[0] = -1;queryID[1] = -1; lastTime=0.0; lastTiming=0.0;}
	};

protected:
	bool m_enabled;

public:
	OpenGLTimings() : m_enabled(true){}
	std::unordered_map<std::string, Timestamp> m_timestamps;
	std::unordered_map<std::string, Timer> m_timers;
	std::unordered_map<std::string, TimerElapsed> m_timersElapsed;
	void timestamp(const std::string& timestamp);
	void beginTimer(const std::string& timer);
	void stopTimer(const std::string& timer);
	void beginTimerElapsed(const std::string& timer); //!< caution: Do not use if other TIME_ELAPSED queries are issued in between!!!
	void stopTimerElapsed(); //!< caution: Do not use if other TIME_ELAPSED queries are issued in between!!!
	void resetTimer(const std::string& timer){}
	void updateReadyTimings();
	Timer waitForTimerResult(const std::string& timer); //!< caution: might freeze?
	Timestamp waitForTimestampResult(const std::string& timestamp); //!< caution: might freeze?
	inline void setEnabled(bool enabled){m_enabled = enabled;};
};


#endif
