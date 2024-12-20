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
#include "CustomMessage_m.h" // Include the generated header for CustomMessage
#include "Node.h"
#include <string>
#include <sstream>
#include <bitset>

Define_Module(Node);

void Node::readFile(const char *filename)
{
    //    const char *fileName = par("inputFile").stringValue();
    std::ifstream file;

    file.open(filename, std::ifstream::in);

    if (!file.is_open())
    {
        EV << "Failed to open " << filename << "\n";
        return;
    }

    // Load messages from input file
    std::string line;
    while (std::getline(file, line))
    {
        std::string errorCode = line.substr(0, 4);
        messages.push_back(line);
    }
    file.close();
}

void Node::initialize()
{
    // Get parameters from ini file
    windowSize = par("windowSize").intValue();
    maxSeqNumber = 7;        // par("maxSeqNumber").intValue();
    timeoutInterval = 10;    // par("timeoutInterval").doubleValue();
    processingTime = 0.5;    // par("processingTime").doubleValue();
    transmissionDelay = 1.0; // par("transmissionDelay").doubleValue();
    errorDelay = 0.4;        // par("errorDelay").doubleValue();
    duplicationDelay = 0.1;  // par("duplicationDelay").doubleValue();
    lossProb = 0;            // par("lossProb").doubleValue();

    // Initialize window tracking
    senderWindow.resize(windowSize);
    ackReceived.resize(windowSize, false);
    timers.resize(windowSize, nullptr);

    nextFrameToSend = 0;
    expectedFrameToReceive = 0;
    totalFramesAccepted = 0;
    lastFrameTime = 0;
}

void Node::handleMessage(cMessage *msg)
{
    if (strcmp(msg->getName(), "StartTransmission") == 0)
    {
        // Initialize transmission
        int me = getIndex();
        readFile(me == 0 ? "input3.txt" : "input3.txt");
        EV << "Node " << me << " initialized with " << messages.size() << " messages.\n";
        sendFrames();
        delete msg;
    }
    else if (msg->isSelfMessage())
    {
        // Processing delay or timeout
        if (strcmp(msg->getName(), "ProcessingDelay") == 0)
        {
            delete msg;
            sendFrames();
        }
        else
        {
            // Handle timeout
            int seqNum = std::stoi(msg->getName());
            handleTimeout(seqNum);
            delete msg;
        }
    }
    else if (CustomMessage *cmsg = dynamic_cast<CustomMessage *>(msg))
    {
        switch (cmsg->getM_Type())
        {
        case FRAME_ACK:
        case FRAME_NACK:
            handleAck(msg);
            break;
        case FRAME_DATA:
            receiveFrame(msg);
            break;
        default:
            delete msg;
        }
    }
    else
    {
        delete msg;
    }
}

void Node::sendFrames()
{
    for (int i = 0; i < windowSize && nextFrameToSend < messages.size(); i++)
    {
        if (!ackReceived[i])
        {
            std::string message = messages[nextFrameToSend];
            std::string payload = message.substr(5); // Skip error code
            std::string errorCode = message.substr(0, 4);
            std::string framedPayload = byteStuff(payload);
            char crc = computeCRC(framedPayload);

            logEvent("Introducing channel error with code =[" + errorCode + "]");

            CustomMessage *frame = new CustomMessage("DataFrame");
            frame->setM_Header(nextFrameToSend % windowSize);
            frame->setM_Payload(framedPayload.c_str());
            frame->setM_Trailer(crc);
            frame->setM_Type(FRAME_DATA);

            // Set timer
            if (timers[i] == nullptr)
            {
                timers[i] = new cMessage(std::to_string(nextFrameToSend % windowSize).c_str());
                scheduleAt(simTime() + timeoutInterval, timers[i]);
            }

            simulateErrors(frame, errorCode, i);

            senderWindow[i] = payload;
            nextFrameToSend++;

            // Add processing delay between frames
            scheduleAt(simTime() + processingTime, new cMessage("ProcessingDelay"));
        }
    }
}

void Node::handleAck(cMessage *msg)
{
    CustomMessage *ackMsg = check_and_cast<CustomMessage *>(msg);
    int ackNum = ackMsg->getM_Header();
    bool isNack = (ackMsg->getM_Type() == FRAME_NACK);
    bool lost = false;

    if (isNack)
    {

        // Cancel existing timer
        if (timers[ackNum % windowSize])
        {
            cancelEvent(timers[ackNum % windowSize]);
            delete timers[ackNum % windowSize];
            timers[ackNum % windowSize] = nullptr;
        }

        // Schedule retransmission after processing time + 0.001
        CustomMessage *retransFrame = new CustomMessage("RetransFrame");
        retransFrame->setM_Header(ackNum);
        std::string framedPayload = byteStuff(senderWindow[ackNum % windowSize]);
        retransFrame->setM_Payload(framedPayload.c_str());
        retransFrame->setM_Type(FRAME_DATA);

        char crc = computeCRC(framedPayload);
        retransFrame->setM_Trailer(crc);

        // Send error-free after delay
        sendDelayed(retransFrame, processingTime + transmissionDelay, "out");

        // Set new timer
        timers[ackNum % windowSize] = new cMessage(std::to_string(ackNum).c_str());
        scheduleAt(simTime() + timeoutInterval, timers[ackNum % windowSize]);
    }
    else
    {
        // Handle cumulative ACKs
        for (int i = seqNum; i <= ackNum; i++)
        {
            if (!ackReceived[i % windowSize])
            {
                ackReceived[i % windowSize] = true;
                if (timers[i % windowSize])
                {
                    cancelEvent(timers[i % windowSize]);
                    delete timers[i % windowSize];
                    timers[i % windowSize] = nullptr;
                }
            }
        }

        // Slide window
        while (!senderWindow.empty() && ackReceived[seqNum % windowSize])
        {
            ackReceived[seqNum % windowSize] = false;
            senderWindow[seqNum % windowSize].clear();
            seqNum++;
        }

        // Try sending new frames
        sendFrames();
    }
    delete ackMsg;
}

void Node::receiveFrame(cMessage *msg)
{
    CustomMessage *dataMsg = check_and_cast<CustomMessage *>(msg);
    int seqNum = dataMsg->getM_Header();
    std::string payload = dataMsg->getM_Payload();
    char crc = dataMsg->getM_Trailer();

    if (checkCRC(payload, crc))
    {
        if (seqNum == expectedFrameToReceive)
        {
            std::string originalPayload = byteUnstuff(payload);
            logEvent("Uploading payload=[" + originalPayload + "] and seq_num=[" +
                     std::to_string(seqNum) + "] to the network layer");

            totalFramesAccepted++;
            lastFrameTime = simTime();
            expectedFrameToReceive = (expectedFrameToReceive + 1) % windowSize;

            // Send ACK
            CustomMessage *ack = new CustomMessage("ACK");
            ack->setM_Header(seqNum);
            ack->setM_Type(FRAME_ACK);
            sendDelayed(ack, processingTime, "out");

            bool lost = false;
            logEvent("Sending [ACK] with number [" + std::to_string(seqNum) +
                     "], loss[" + (lost ? "Yes" : "No") + "] ");
        }
    }
    else
    {

        EV << ("CRC error detected for frame " + std::to_string(seqNum));

        // Send NACK for in-order frames only
        if (seqNum == expectedFrameToReceive)
        {
            CustomMessage *nack = new CustomMessage("NACK");
            nack->setM_Header(seqNum);
            nack->setM_Type(FRAME_NACK);
            send(nack, "out");

            bool lost = false;
            logEvent("Sending [NACK] with number [" + std::to_string(seqNum) +
                     "], loss[" + (lost ? "Yes" : "No") + "] ");
        }
    }
    delete msg;
}

std::string Node::byteStuff(const std::string &payload)
{
    std::ostringstream framed;
    framed << '$'; // Start flag
    for (char c : payload)
    {
        if (c == '$' || c == '/')
        {
            framed << '/'; // Escape special characters
        }
        framed << c;
    }
    framed << '$'; // End flag
    return framed.str();
}

std::string Node::byteUnstuff(const std::string &framed)
{
    std::ostringstream payload;
    bool escape = false;
    for (size_t i = 1; i < framed.size() - 1; ++i)
    { // Skip start and end flags
        if (framed[i] == '/')
        {
            escape = true;
            continue;
        }
        payload << framed[i];
        escape = false;
    }
    return payload.str();
}

char Node::computeCRC(const std::string &data)
{
    unsigned char polynomial = 0x07; // x8 + x2 + x + 1
    unsigned char crc = 0xFF;        // Initialize with all 1's

    for (unsigned char c : data)
    {
        crc ^= c;
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 0x80) ? ((crc << 1) ^ polynomial) : (crc << 1);
        }
    }
    return static_cast<char>(crc);
}

bool Node::checkCRC(const std::string &data, char crc)
{
    EV << "Checking CRC for data: " << data << endl;
    unsigned char receivedCRC = static_cast<unsigned char>(crc);
    unsigned char calculatedCRC = static_cast<unsigned char>(computeCRC(data));
    EV << "Calculated CRC: " << (int)calculatedCRC << ", Received CRC: " << (int)receivedCRC << endl;
    return calculatedCRC == receivedCRC;
}

void Node::handleTimeout(int seqNum)
{
    // Create error-free retransmission
    CustomMessage *frame = new CustomMessage("DataFrame");
    frame->setM_Header(seqNum);

    std::string payload = senderWindow[seqNum % windowSize];
    std::string framedPayload = byteStuff(payload);
    char crc = computeCRC(framedPayload);

    frame->setM_Payload(framedPayload.c_str());
    frame->setM_Trailer(crc);
    frame->setM_Type(FRAME_DATA);

    logEvent("Timeout retransmission for frame " + std::to_string(seqNum));

    // Send with additional processing delay
    sendDelayed(frame, processingTime + 0.001, "out");

    // Reset timer
    timers[seqNum % windowSize] = new cMessage(std::to_string(seqNum).c_str());
    scheduleAt(simTime() + timeoutInterval, timers[seqNum % windowSize]);
}

void Node::simulateErrors(CustomMessage *frame, const std::string &errorCode, int i)
{
    std::string payload = frame->getM_Payload();
    char trailer = frame->getM_Trailer();
    char seqNum = frame->getM_Header();
    bool modified = false;
    bool isLost = false;
    bool isDuplicated = false;
    bool isDelayed = false;
    double duplicationDelay = 0;
    int bitPosition = 0;
    double delay = 0;

    // Bit Modification (1xxx)
    if (errorCode[0] == '1')
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        modified = true;

        frame->setM_Payload(payload.c_str());
        if (timers[i] == nullptr)
        {
            timers[i] = new cMessage(std::to_string(frame->getM_Header()).c_str());
            scheduleAt(simTime() + timeoutInterval, timers[i]);
        }

        EV << ("Frame " + std::to_string(frame->getM_Header()) + " modified at bit " + std::to_string(bitPosition));
    }

    // Loss (x1xx)
    if (errorCode[1] == '1')
    {
        isLost = true;
        EV << ("Frame " + std::to_string(frame->getM_Header()) + " lost");
        delete frame;
        return;
    }

    // Duplication (xx1x)
    if (errorCode[2] == '1')
    {
        isDuplicated = true;
        // duplicationDelay = 0.1; // 100ms delay for duplicate
        CustomMessage *duplicate = frame->dup();
        EV << ("Frame " + std::to_string(frame->getM_Header()) + " duplicated");
        sendDelayed(duplicate, simTime() + duplicationDelay, "out");
    }

    // Delay (xxx1)
    if (errorCode[3] == '1')
    {
        isDelayed = true;
        // delay = 0.2; // 200ms delay
        EV << ("Frame " + std::to_string(frame->getM_Header()) + " delayed");
        sendDelayed(frame, simTime() + errorDelay, "out");
        return;
    }

    // Send frame if not lost or delayed
    if (!isLost && !isDelayed)
    {
        send(frame, "out");
    }

    std::string state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                        "] and payload=[" + payload +
                        "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                        "] , Modified [" + (modified ? std::to_string(bitPosition) : "-1") +
                        "] , Lost [" + (isLost ? "Yes" : "No") +
                        "], Duplicate [" + (isDuplicated ? "1" : "0") +
                        "], Delay [" + (isDelayed ? std::to_string(errorDelay) : "0") + "]";
    logEvent(state);
}

char Node::calculateParity(const std::string &payload)
{
    char parity = 0;
    for (char c : payload)
    {
        parity ^= c; // XOR all the characters to calculate parity
    }
    return parity;
}

void Node::logEvent(const std::string &event)
{
    std::ofstream logFile("output.txt", std::ios::app);
    if (!logFile.is_open())
    {
        EV << "Error opening log file!\n";
        return;
    }

    logFile << "At time [" << simTime() << "], Node[" << getIndex()
            << "]: " << event << "\n";

    logFile.close();
}
