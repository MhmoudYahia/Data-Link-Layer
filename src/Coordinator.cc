//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 
#include "omnetpp.h"  // Required for simtime_t
#include "Coordinator.h"
#include <fstream>      // For file input/output

Define_Module(Coordinator);

void Coordinator::initialize()
{
    const char *fileName = par("inputFile").stringValue();
           std::ifstream file;

           file.open(fileName, std::ifstream::in);

           if (!file.is_open()) {
               EV << "Failed to open " << fileName << "\n";
               return;
           }

           // Read starting node ID and start time

           std::string startTimeStr;
           file >> nodeId >> startTimeStr;
           file.close();

           // Convert startTimeStr to simtime_t
           simtime_t startTime = SimTime::parse(startTimeStr.c_str());

           EV << "Start Time (simtime_t): " << startTime << "s\n";
           // Send start signal to the chosen node
           cMessage *startMsg = new cMessage("StartTransmission");
           scheduleAt(simTime() + 0.1 , startMsg);
           EV << "Coordinator initialized. Node " << nodeId << " will start at " << startTime << "s.\n";
           EV << "Scheduling start message at: " << simTime() + startTime << "s.\n";

}

void Coordinator::handleMessage(cMessage *msg)
{
    EV<<"------------";
    if(msg->isSelfMessage())
        {
            EV << "Sending message from Coordinator to node " << nodeId << ".\n";
            send(msg, "out", nodeId);
        }

}
