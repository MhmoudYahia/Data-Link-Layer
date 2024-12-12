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
        logEvent("Introducing channel error with code =[" + errorCode + "]");
        messages.push_back(line);
    }
    file.close();
}

void Node::initialize()
{
    windowSize = par("windowSize");
    timeoutInterval = par("timeoutInterval");
}

void Node::handleMessage(cMessage *msg)
{
    if (strcmp(msg->getName(), "StartTransmission") == 0)
    {

        int me = getIndex();
        if (me == 0)
        {

            readFile("input0.txt");
        }
        else
        {
            readFile("input1.txt");
        }

        EV << "Node " << me << " initialized with " << messages.size() << " messages.\n";
        sendFrames();
    }
    else if (strcmp(msg->getName(), "ACK") == 0)
    {
        handleAck(msg);
    }
    else if (strcmp(msg->getName(), "Timeout") == 0)
    {
        retransmitFrames();
    }
    else if (strcmp(msg->getName(), "DataFrame") == 0)
    {
        receiveFrame(msg);
    }
}

void Node::sendFrames()
{
    for (int i = 0; i < windowSize && seqNum + i < messages.size(); i++)
    {
        std::string message = messages[seqNum + i];
        std::string payload = message.substr(5); // Skip the 4-bit error code
        std::string framedPayload = byteStuff(payload);
        std::string crc = computeCRC(framedPayload);

        // Create a custom message for sending
        CustomMessage *msg = new CustomMessage("DataFrame");

        char header = static_cast<char>(seqNum + i);
        char trailer = calculateParity(framedPayload);

        msg->setM_Header(header);
        msg->setM_Payload(framedPayload.c_str()); // Set the framed payload
        msg->setM_Trailer(trailer);
        msg->setM_Type(2);

        std::string errorCode = message.substr(0, 4);
        simulateErrors(msg, errorCode, i);
    }
}

void Node::handleAck(cMessage *msg)
{
    CustomMessage *ackMsg = check_and_cast<CustomMessage *>(msg);
    int ackNum = ackMsg->getM_Header(); // Example, using header for ACK sequence number

    logEvent("Received ACK for SeqNum=" + std::to_string(ackNum));
    seqNum = std::max(seqNum, ackNum + 1); // Slide window
    sendFrames();
    delete ackMsg;
}

void Node::retransmitFrames()
{

    logEvent("Timeout event for frame with seq_num=" + std::to_string(seqNum));
    sendFrames();
}

void Node::receiveFrame(cMessage *msg)
{
    CustomMessage *dataMsg = check_and_cast<CustomMessage *>(msg);
    int seqNum = dataMsg->getM_Header(); // Sequence number from the header
    std::string payload = dataMsg->getM_Payload();
    char trailer = dataMsg->getM_Trailer();
    std::string crc = computeCRC(payload);
    EV << "Frame received: " << payload << " with seq: " << seqNum << endl;

    if (checkCRC(payload, crc))
    {
        std::string originalPayload = byteUnstuff(payload);
        logEvent("Uploading payload=[" + originalPayload + "] and seq_num=[" + std::to_string(seqNum) + "] to the network layer");
        // Send ACK
        CustomMessage *ack = new CustomMessage("ACK");
        ack->setM_Header(seqNum); // Acknowledge the received sequence number
        ack->setM_Type(1);        // Set type to ACK
        send(ack, "out");
    }
    else
    {
        //        logEvent("", 6, payload.c_str(), seqNum);
        // Send NACK
        CustomMessage *nack = new CustomMessage("NACK");
        nack->setM_Header(seqNum); // NACK for the corrupted frame
        nack->setM_Type(0);        // Set type to NACK
        send(nack, "out");
    }
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

std::string Node::computeCRC(const std::string &data)
{
    // CRC-8 polynomial: x^8 + x^2 + x^1 + 1 (0x07)
    const uint8_t polynomial = 0x07;
    uint8_t crc = 0;

    // Process each byte in the data
    for (char byte : data)
    {
        crc ^= byte;
        // Process each bit
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x80)
            {
                crc = (crc << 1) ^ polynomial;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    // Convert CRC to string representation
    std::stringstream ss;
    ss << std::hex << static_cast<int>(crc);
    return ss.str();
}

bool Node::checkCRC(const std::string &data, const std::string &crc)
{
    return computeCRC(data) == crc;
}

void Node::simulateErrors(CustomMessage *frame, const std::string &errorCode, int i)
{
    std::string payload = frame->getM_Payload(); // Retrieve payload from CustomMessage
    char trailer = frame->getM_Trailer();        // Retrieve trailer from CustomMessage
    bool modified = false;
    int bitPosition = -1;
    bool isLost = false;
    bool isDuplicated = false;
    bool isDelayed = false;
    double duplicationDelay = 0;
    double delay = 0;

    // Modification
    if (errorCode[0] == '1')
    {
        modified = true;
        int bitToFlip = intuniform(0, payload.size() * 8 - 1); // Select a random bit to flip
        int charIndex = bitToFlip / 8;
        int bitInChar = bitToFlip % 8;
        bitPosition = bitToFlip;

        payload[charIndex] ^= (1 << bitInChar); // Flip the chosen bit
        frame->setM_Payload(payload.c_str());   // Update the modified payload

        EV << "Modified bit " << bitToFlip << " in payload.\n";
    }

    // Loss
    if (errorCode[1] == '1')
    {
        isLost = true;
        EV << "Frame lost due to error code.\n";
        delete frame; // Delete the frame to simulate loss
    }

    // Duplication
    if (errorCode[2] == '1')
    {
        duplicationDelay = par("duplicationDelay").doubleValue();
        CustomMessage *dupFrame = frame->dup(); // Duplicate the frame
        sendDelayed(dupFrame, duplicationDelay, "out");
        EV << "Duplicated frame sent with delay.\n";
    }

    // Delay

    if (errorCode[3] == '1' && isLost == false)
    {
        delay = par("errorDelay").doubleValue();
        sendDelayed(frame, delay, "out");
        EV << "Frame delayed due to error code.\n";
    }
    else if (isLost == false)
    {
        send(frame, "out"); // Send the frame normally
        EV << "Frame sent without delay.\n";
    }
    // Log the frame transmission details
    logEvent("Sent frame with seq_num=[" + std::to_string(seqNum + i) +
             "] and payload=[" + payload +
             "] and trailer=[" + std::to_string(trailer) +
             "], Modified [" + (modified ? std::to_string(bitPosition) : "-1") +
             "], Lost [" + (isLost ? "Yes" : "No") +
             "], Duplicate [" + (duplicationDelay > 0 ? std::to_string(duplicationDelay) : "0") +
             "], Delay [" + (delay > 0 ? std::to_string(delay) : "0") + "]");
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