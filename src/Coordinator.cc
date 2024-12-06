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

           double startTime;
           file >> nodeId >> startTime;
           file.close();
           EV<< nodeId <<"------"<< startTime;
           // Send start signal to the chosen node
           cMessage *startMsg = new cMessage("StartTransmission");
           scheduleAt(simTime()+ startTime , startMsg);
           EV << "Coordinator initialized. Node " << nodeId << " will start at " << startTime << "s.\n";
}

void Coordinator::handleMessage(cMessage *msg)
{
    if(msg->isSelfMessage())
        {
            send(msg, "out", nodeId);
        }

}
