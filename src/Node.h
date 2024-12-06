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

#ifndef __DATALINKLAYERNETWORK_NODE_H_
#define __DATALINKLAYERNETWORK_NODE_H_

#include <omnetpp.h>
#include <fstream>      // For file input/output

using namespace omnetpp;

/**
 * TODO - Generated class
 */
class Node : public cSimpleModule
{
private:
    std::vector<std::string> messages;
    int seqNum = 0; // Current sequence number
    int windowSize; // Sliding window size
    int timeoutInterval; // Timeout interval in seconds
    std::ofstream logFile; // Log file

    void sendFrames();
    void handleAck(cMessage *msg);
    void retransmitFrames();
    void receiveFrame(cMessage *msg);
    void readFile(const char *filename);
    // Error simulation and utilities
    std::string byteStuff(const std::string &payload);
    std::string byteUnstuff(const std::string &framed);
    std::string computeCRC(const std::string &data);
    bool checkCRC(const std::string &data, const std::string &crc);
    void simulateErrors(CustomMessage *frame, const std::string &errorCode);
    void logEvent(const std::string &event);
    char calculateParity(const std::string& payload);

 protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

#endif
