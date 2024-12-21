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
    errorDelay = 4;          // par("errorDelay").doubleValue();
    duplicationDelay = 0.1;  // par("duplicationDelay").doubleValue();
    lossProb = 0;            // par("lossProb").doubleValue();

    timers.resize(windowSize, nullptr);

    nextFrameToSend = 0;
    expectedFrameToReceive = 0;
    totalFramesAccepted = 0;
    lastFrameTime = 0;

    baseIndex = 0;
    currentIndex = 0;
    senderWindow.resize(windowSize);
    ackReceived.resize(windowSize, false);
    frameReceived.resize(windowSize, false);
    receiverBuffer.resize(windowSize);
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
        if (strcmp(msg->getName(), "SendNextFrame") == 0)
        {
            // send next frame
            sendFrames();
            delete msg;
        }
        else if (strcmp(msg->getName(), "Print") == 0)
        {

            std::string taher = prints.front();
            prints.pop();
            EV << "Taher: " << taher << endl;
            logEvent(taher);
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
    if (currentIndex < messages.size() &&
        currentIndex < baseIndex + windowSize &&
        !ackReceived[currentIndex % maxSeqNumber])
    {
        std::string message = messages[currentIndex];

        std::string payload = message.substr(5); // Skip error code
        std::string errorCode = message.substr(0, 4);
        std::string framedPayload = byteStuff(payload);
        char crc = computeCRC(framedPayload);

        CustomMessage *frame = new CustomMessage("DataFrame");
        frame->setM_Header(currentIndex % maxSeqNumber); // TODO: Check if this is correct
        frame->setM_Payload(framedPayload.c_str());
        frame->setM_Trailer(crc);
        frame->setM_Type(FRAME_DATA);

        logEvent("Introducing channel error with code =[" + errorCode + "]");
        // Set timer
        // if (timers[i] == nullptr)
        // {
        //     timers[i] = new cMessage(std::to_string(nextFrameToSend % windowSize).c_str());
        //     scheduleAt(simTime() + timeoutInterval, timers[i]);
        // }

        simulateErrors(frame, errorCode, 0);

        // Update window state
        senderWindow[currentIndex % maxSeqNumber] = payload;
        currentIndex++;

        // Schedule next frame after processing delay
        if (currentIndex < messages.size())
        {
            scheduleAt(simTime() + processingTime, new cMessage("SendNextFrame"));
        }
    }
}

void Node::handleAck(cMessage *msg)
{
    CustomMessage *ackMsg = check_and_cast<CustomMessage *>(msg);
    int ackNum = ackMsg->getM_Header();

    if (ackMsg->getM_Type() == FRAME_NACK)
    {
        if (ackNum >= baseIndex && ackNum < currentIndex)
        {
            // Cancel existing timer
            if (timers[ackNum % maxSeqNumber])
            {
                cancelEvent(timers[ackNum % maxSeqNumber]);
                delete timers[ackNum % maxSeqNumber];
                timers[ackNum % maxSeqNumber] = nullptr;
            }

            // Retransmit frame
            std::string framedPayload = byteStuff(senderWindow[ackNum % windowSize]);
            CustomMessage *retransFrame = new CustomMessage("RetransFrame");
            retransFrame->setM_Header(ackNum);
            retransFrame->setM_Payload(framedPayload.c_str());
            retransFrame->setM_Trailer(computeCRC(framedPayload));
            retransFrame->setM_Type(FRAME_DATA);

            sendDelayed(retransFrame, processingTime + transmissionDelay, "out");

            // Set new timer
            timers[ackNum % maxSeqNumber] = new cMessage(std::to_string(ackNum).c_str());
            scheduleAt(simTime() + timeoutInterval, timers[ackNum % maxSeqNumber]);
        }
    }
    else
    {
        // Process cumulative ACK
        if (ackNum >= baseIndex)
        {
            // Clear all frames up to ackNum
            for (int i = baseIndex; i <= ackNum; i++)
            {
                if (timers[i % maxSeqNumber])
                {
                    cancelEvent(timers[i % maxSeqNumber]);
                    delete timers[i % maxSeqNumber];
                    timers[i % maxSeqNumber] = nullptr;
                }
                ackReceived[i % windowSize] = false;
                senderWindow[i % windowSize].clear();
            }

            // Slide window
            baseIndex = ackNum + 1;

            // Try sending new frames
            scheduleAt(simTime() + processingTime, new cMessage("SendNextFrame"));
        }
    }
    delete msg;
}

// void Node::receiveFrame(cMessage *msg)
// {
//     CustomMessage *frame = check_and_cast<CustomMessage *>(msg);
//     int rcvSeqNum = frame->getM_Header();
//     std::string payload = frame->getM_Payload();
//     char crc = frame->getM_Trailer();

//     bool isCorerctSeqnum = false;

//     for (size_t i = 0; i < windowSize; i++)
//     {
//         if (rcvSeqNum == (baseIndex + i) % (maxSeqNumber + 1))
//         {
//             isCorerctSeqnum = true;
//             break;
//         }
//     }

//     // Check if frame is within window bounds
//     if (isCorerctSeqnum)
//     {
//         // Check CRC
//         if (checkCRC(payload, crc))
//         {
//             // Buffer the frame
//             receiverBuffer[maxSeqNumber + 1] = byteUnstuff(payload);
//             frameReceived[maxSeqNumber + 1] = true; // Mark frame as received

//             int temp = baseIndex;
//             while (frameReceived[baseIndex])
//             {
//                 // Process and deliver to network layer
//                 logEvent("Uploading payload=[" +
//                          receiverBuffer[baseIndex] +
//                          "] and seq_num=[" + std::to_string(baseIndex) +
//                          "] to the network layer");

//                 // Clear buffer and mark as unreceived
//                 receiverBuffer[baseIndex] = "";
//                 frameReceived[baseIndex] = false;
//                 baseIndex++;
//             }

//             // Send cumulative ACK for highest consecutive frame
//             if (baseIndex != temp)
//             {
//                 CustomMessage *ack = new CustomMessage("ACK");
//                 ack->setM_Header(baseIndex); // ACK all frames up to this
//                 ack->setM_Type(FRAME_ACK);
//                 sendDelayed(ack, processingTime + transmissionDelay, "out");

//                 logEvent("Sending [ACK] with number [" +
//                          std::to_string(baseIndex - 1) +
//                          "], loss[No] ");

//                 expectedFrameToReceive = baseIndex;
//             }
//         }
//         else
//         {
//             EV << "Invalid CRC\n";
//             // Invalid CRC
//             if (rcvSeqNum == expectedFrameToReceive)
//             {
//                 // Send NACK for expected frame
//                 CustomMessage *nack = new CustomMessage("NACK");
//                 nack->setM_Header(rcvSeqNum);
//                 nack->setM_Type(FRAME_NACK);
//                 sendDelayed(nack, processingTime + transmissionDelay, "out");

//                 logEvent("Sending [NACK] with number [" +
//                          std::to_string(rcvSeqNum) +
//                          "], loss[No] ");
//             }
//             // Else: Silent discard for out-of-sequence corrupt frames
//         }
//     }
//     // Else: Frame outside window - silently discard

//     delete msg;
// }

void Node::receiveFrame(cMessage *msg)
{
    CustomMessage *frame = check_and_cast<CustomMessage *>(msg);
    int rcvSeqNum = frame->getM_Header();
    std::string payload = frame->getM_Payload();
    char crc = frame->getM_Trailer();

    bool isCorerctSeqnum = false;

    for (size_t i = 0; i < windowSize; i++)
    {
        if (rcvSeqNum == (baseIndex + i) % (maxSeqNumber + 1))
        {
            isCorerctSeqnum = true;
            break;
        }
    }
    // Check if frame is within window bounds
    if (isCorerctSeqnum)
    {
        EV << "-------------------------------------------------------\n";
        // Check CRC
        if (checkCRC(payload, crc))
        {
            // Buffer the frame
            receiverBuffer[rcvSeqNum % maxSeqNumber] = byteUnstuff(payload);
            frameReceived[rcvSeqNum % maxSeqNumber] = true; // Mark frame as received

            // If it's the expected frame
            int highestConsecutive = expectedFrameToReceive;

            while (frameReceived[highestConsecutive % maxSeqNumber])
            {
                // Process and deliver to network layer
                logEvent("Uploading payload=[" +
                         receiverBuffer[highestConsecutive % maxSeqNumber] +
                         "] and seq_num=[" + std::to_string(highestConsecutive) +
                         "] to the network layer");

                // Clear buffer and mark as unreceived
                receiverBuffer[highestConsecutive % maxSeqNumber] = "";
                frameReceived[highestConsecutive % maxSeqNumber] = false;
                highestConsecutive++;
            }

            // Send cumulative ACK for highest consecutive frame
            if (highestConsecutive > expectedFrameToReceive)
            {
                CustomMessage *ack = new CustomMessage("ACK");
                ack->setM_Header(highestConsecutive); // ACK all frames up to this
                ack->setM_Type(FRAME_ACK);
                sendDelayed(ack, processingTime + transmissionDelay, "out");

                logEvent("Sending [ACK] with number [" +
                         std::to_string(highestConsecutive) +
                         "], loss[No] ");

                expectedFrameToReceive = highestConsecutive;
            }
        }
        else
        {
            EV << "Invalid CRC\n";
            // Invalid CRC
            if (rcvSeqNum == expectedFrameToReceive)
            {
                // Send NACK for expected frame
                CustomMessage *nack = new CustomMessage("NACK");
                nack->setM_Header(rcvSeqNum);
                nack->setM_Type(FRAME_NACK);
                sendDelayed(nack, processingTime + transmissionDelay, "out");

                logEvent("Sending [NACK] with number [" +
                         std::to_string(rcvSeqNum) +
                         "], loss[No] ");
            }
            // Else: Silent discard for out-of-sequence corrupt frames
        }
    }
    // Else: Frame outside window - silently discard

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
    int bitPosition = 0;
    std::string state = "";
    // Case "0000": No error
    if (errorCode == "0000")
    {
        sendDelayed(frame, processingTime + transmissionDelay, "out");
        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [-1], Lost [No], Duplicate [0], Delay [0]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));
    }
    // Case "0001": Delay only
    else if (errorCode == "0001")
    {
        sendDelayed(frame, processingTime + transmissionDelay + errorDelay, "out");
        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [-1], Lost [No], Duplicate [0], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));
    }
    // Case "0010": Duplication without delay
    else if (errorCode == "0010")
    {
        CustomMessage *duplicate = frame->dup();
        sendDelayed(frame, processingTime + transmissionDelay, "out");
        sendDelayed(duplicate, processingTime + transmissionDelay + duplicationDelay, "out");
        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [-1], Lost [No], Duplicate [1], Delay [0]";
        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [-1], Lost [No], Duplicate [2], Delay [0]";

        prints.push(state2);
        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));
    }
    // Case "0011": Duplication with delay
    else if (errorCode == "0011")
    {
        CustomMessage *duplicate = frame->dup();
        sendDelayed(frame, processingTime + transmissionDelay + errorDelay, "out");
        sendDelayed(duplicate, processingTime + transmissionDelay + errorDelay + duplicationDelay, "out");
        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [-1], Lost [No], Duplicate [1], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [-1], Lost [No], Duplicate [2], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state2);
        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));
    }
    // Case "1100": Loss with modification
    else if (errorCode == "1100")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        modified = true;

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [Yes], Duplicate [1], Delay [0]";
        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        delete frame;
        return;
    }
    // Case "1101": Loss with modification and delay
    else if (errorCode == "1101")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        modified = true;

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] ,Modified [" + std::to_string(bitPosition) +
                "], Lost [Yes], Duplicate [0], Delay [" + std::to_string(errorDelay) + "]";
        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));
        delete frame;
        return;
    }
    // Case "1110": Loss with modification and duplication
    else if (errorCode == "1110")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        modified = true;

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [Yes], Duplicate [1], Delay [0]";
        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [" + std::to_string(bitPosition) +
                             "], Lost [Yes], Duplicate [2], Delay [0]";

        prints.push(state2);
        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));

        delete frame;
        return;
    }
    // Case "1111": Loss with modification, duplication and delay
    else if (errorCode == "1111")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        modified = true;

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [Yes], Duplicate [1], Delay [" + std::to_string(errorDelay) + "]";
        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [" + std::to_string(bitPosition) +
                             "], Lost [Yes], Duplicate [2], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state2);
        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));

        delete frame;
        return;
    }
    // Case "1000": Modification only
    else if (errorCode == "1000")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        frame->setM_Payload(payload.c_str());
        modified = true;
        sendDelayed(frame, processingTime + transmissionDelay, "out");
        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [No], Duplicate [0], Delay [0]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));
    }
    // Case "1001": Modification and delay
    else if (errorCode == "1001")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        frame->setM_Payload(payload.c_str());
        modified = true;
        sendDelayed(frame, processingTime + transmissionDelay + errorDelay, "out");

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [No], Duplicate [0], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));
    }
    // Case "1010": Modification and duplication
    else if (errorCode == "1010")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        frame->setM_Payload(payload.c_str());
        modified = true;
        CustomMessage *duplicate = frame->dup();
        sendDelayed(frame, processingTime + transmissionDelay, "out");
        sendDelayed(duplicate, processingTime + transmissionDelay + duplicationDelay, "out");

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [No], Duplicate [1], Delay [0]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [" + std::to_string(bitPosition) +
                             "], Lost [No], Duplicate [2], Delay [0]";

        prints.push(state2);
        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));
    }
    // Case "1011": Modification, duplication and delay
    else if (errorCode == "1011")
    {
        bitPosition = rand() % (payload.length() * 8);
        char &byte = payload[bitPosition / 8];
        byte ^= (1 << (bitPosition % 8));
        frame->setM_Payload(payload.c_str());
        modified = true;
        CustomMessage *duplicate = frame->dup();
        sendDelayed(frame, processingTime + transmissionDelay + errorDelay, "out");
        sendDelayed(duplicate, processingTime + transmissionDelay + errorDelay + duplicationDelay, "out");

        state = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                "] and payload=[" + payload +
                "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                "] , Modified [" + std::to_string(bitPosition) +
                "], Lost [No], Duplicate [1], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state);
        scheduleAt(simTime() + processingTime, new cMessage("Print"));

        std::string state2 = "[sent] frame with seq_num=[" + std::to_string(seqNum) +
                             "] and payload=[" + payload +
                             "] and trailer=[" + std::bitset<8>(trailer).to_string() +
                             "] , Modified [" + std::to_string(bitPosition) +
                             "], Lost [No], Duplicate [2], Delay [" + std::to_string(errorDelay) + "]";

        prints.push(state2);

        scheduleAt(simTime() + processingTime + duplicationDelay, new cMessage("Print"));
    }
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

void Node::logEvent(const std::string &event, int i)
{
    std::ofstream logFile("output.txt", std::ios::app);
    if (!logFile.is_open())
    {
        EV << "Error opening log file!\n";
        return;
    }

    logFile << "At time [" << simTime() + i << "], Node[" << getIndex()
            << "]: " << event << "\n";

    logFile.close();
}
