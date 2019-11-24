/*
 * TimeBudgetter.cpp
 *
 *  Created on: Nov 12, 2019
 *      Author: reddi-rtx
 */

#include "TimeBudgetter.h"

TimeBudgetter::TimeBudgetter(double maxSensorRange, double maxVelocity, std::vector<double> accelerationCoeffs, double timeIncr)
			:sensorActuatorModel_(maxSensorRange, maxVelocity, accelerationCoeffs),
			timeIncr_(timeIncr){}



// simple vector magnitude calculation
double TimeBudgetter::calc_magnitude(double x, double y, double z) {
  return std::sqrt(x*x + y*y + z*z);
}


// for a fixed V (i.e., if the drone's v doesn't change), how much budget
double TimeBudgetter::calcSamplingTimeFixV(double velocityMag, double sensorRange, std::vector<double> acceleartionCoeffs, double latency){
	return this->sensorActuatorModel_.worseCaseResponeTime(velocityMag, sensorRange, acceleartionCoeffs )- latency;
}

double TimeBudgetter::calcSamplingTimeFixV(double velocityMag, double sensorRange, double latency){
	return this->sensorActuatorModel_.worseCaseResponeTime(velocityMag, sensorRange , this->sensorActuatorModel_.accelerationCoeffs()) - latency;
}


double TimeBudgetter::calcSamplingTimeFixV(double velocityMag, double latency){
	return this->sensorActuatorModel_.worseCaseResponeTime(velocityMag, this->sensorActuatorModel_.maxSensorRange(), this->sensorActuatorModel_.accelerationCoeffs()) - latency;
}



// calcSamplingTime helper (called recursively)
void TimeBudgetter::calcSamplingTimeHelper(std::deque<multiDOFpoint>::iterator trajBegin, std::deque<multiDOFpoint>::iterator trajEnd,
		std::deque<multiDOFpoint>::iterator &trajItr, double &nextSamplingTime, double latency){
	multiDOFpoint point = *(trajBegin);
	double velocity_magnitude = calc_magnitude(point.vx, point.vy, point.vz);
	double BudgetTillNextSample = calcSamplingTimeFixV(velocity_magnitude, latency);
	double potentialBudgetTillNextSample;  // a place holder that gets updated
	std::deque<multiDOFpoint>::iterator trajItrTemp =  trajBegin;  //pointing to the sample point we are considering at the moment

	// corener case
	if (BudgetTillNextSample <= 0) {
		//std::cout<<"shoudn't get sample time less than zero; probaly went over the v limit"<<std::endl;
		nextSamplingTime = this->timeIncr_;
		trajItr += 1;
		return;
	}

	double nextSamplingTimeTemp = 0;

	while (BudgetTillNextSample > 0 && trajItrTemp != trajEnd){
		BudgetTillNextSample -= this->timeIncr_;
		nextSamplingTimeTemp += this->timeIncr_;
		point = *(trajItrTemp);
		velocity_magnitude = calc_magnitude(point.vx, point.vy, point.vz);
		potentialBudgetTillNextSample = calcSamplingTimeFixV(velocity_magnitude, latency);
		if (potentialBudgetTillNextSample <= 0) {
			//std::cout<<"-- shoudn't get sample time less than zero; probaly went over the v limit"<<std::endl;
			trajItrTemp +=1;
			nextSamplingTimeTemp +=  this->timeIncr_;
			break;
		}
		BudgetTillNextSample = std::min(potentialBudgetTillNextSample, BudgetTillNextSample);
		trajItrTemp +=1;
	}
	trajItr = trajItrTemp;
	nextSamplingTime = nextSamplingTimeTemp;
}


// iteratively going through all the points in the trajectory and calculating the time budget by
// calling its Helper
std::vector<double> TimeBudgetter::calcSamplingTime(trajectory_t traj, double latency){
	double thisSampleTime = 0;
	double nextSampleTime = 0;
	this->SamplingTimes_.clear();
	this->SamplingTimes_.push_back(thisSampleTime);

	std::deque<multiDOFpoint>::iterator trajRollingItrBegin = traj.begin();
	std::deque<multiDOFpoint>::iterator trajItrEnd = traj.end();
	std::deque<multiDOFpoint>::iterator trajItr = traj.begin();

	while (trajRollingItrBegin < trajItrEnd){
		calcSamplingTimeHelper(trajRollingItrBegin, trajItrEnd, trajItr, nextSampleTime, latency);
		thisSampleTime += nextSampleTime;
		this->SamplingTimes_.push_back(thisSampleTime);
		trajRollingItrBegin = trajItr;
	}
	return this->SamplingTimes_;
}


// destructor
TimeBudgetter::~TimeBudgetter() {
	// TODO Auto-generated destructor stub
}