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
#include <fstream> // For file input/output

using namespace omnetpp;

/**
 * TODO - Generated class
 */
class Node : public cSimpleModule
{
private:
    std::vector<std::string> messages;
    int seqNum = 0;      // Current sequence number
    int windowSize;      // Sliding window size
    double timeoutInterval; // Timeout interval in seconds
    double processingTime;
    double transmissionDelay;
    double errorDelay; 
    double duplicationDelay;
    double lossProb;
    int maxSeqNumber; // Maximum sequence number
    std::ofstream logFile; // Log file

    std::vector<std::string> senderWindow; // Buffer for sender window
    std::vector<bool> ackReceived;         // Track received ACKs
    std::vector<cMessage *> timers;        // Timers for frames
    int nextFrameToSend;                   // Next frame to send
    int expectedFrameToReceive;            // Expected frame at receiver
    int totalFramesAccepted;               // Counter for accepted frames
    simtime_t lastFrameTime;               // Time of last accepted frame

    // Constants
    static const int FRAME_DATA = 2;
    static const int FRAME_ACK = 1;
    static const int FRAME_NACK = 0;

    void sendFrames();
    void handleAck(cMessage *msg);
    void receiveFrame(cMessage *msg);
    void readFile(const char *filename);
    // Error simulation and utilities
    std::string byteStuff(const std::string &payload);
    std::string byteUnstuff(const std::string &framed);
    char computeCRC(const std::string &data);
    bool checkCRC(const std::string &data, char crc);
    void simulateErrors(CustomMessage *frame, const std::string &errorCode, int i);
    void logEvent(const std::string &event);
    char calculateParity(const std::string &payload);
    void handleTimeout(int seqNum);

protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

#endif
