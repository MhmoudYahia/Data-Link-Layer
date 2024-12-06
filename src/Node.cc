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
#include "CustomMessage_m.h"  // Include the generated header for CustomMessage
#include "Node.h"
#include <string>
#include <sstream>
#include <bitset>


Define_Module(Node);

void Node::readFile(const char *filename){
    const char *fileName = par("inputFile").stringValue();
          std::ifstream file;

          file.open(fileName, std::ifstream::in);

          if (!file.is_open()) {
              EV << "Failed to open " << fileName << "\n";
              return;
          }

          // Load messages from input file
          std::string line;
          while (std::getline(file, line)) {
              messages.push_back(line);
          }
          file.close();

}

void Node::initialize()
{
    windowSize = par("windowSize");
    timeoutInterval = par("timeoutInterval");

    logFile.open("output.txt", std::ios::app);
    logFile << "Node initialized with " << messages.size() << " messages.\n";
    logFile.close();
}

void Node::handleMessage(cMessage *msg)
{
    if (strcmp(msg->getName(), "StartTransmission") == 0) {
        int me = getIndex();
           if(me == 0)
           {
              readFile("input0.txt");
           }
           else
           {
              readFile("input1.txt");
           }
           sendFrames();
       } else if (strcmp(msg->getName(), "ACK") == 0) {
           handleAck(msg);
       } else if (strcmp(msg->getName(), "Timeout") == 0) {
           retransmitFrames();
       } else if (strcmp(msg->getName(), "DataFrame") == 0) {
           receiveFrame(msg);
    }
}


void Node::sendFrames() {
    for (int i = 0; i < windowSize && seqNum + i < messages.size(); i++) {
        std::string message = messages[seqNum + i];
        std::string payload = message.substr(5); // Skip the 4-bit error code
        std::string framedPayload = byteStuff(payload);
        std::string crc = computeCRC(framedPayload);

        // Create a custom message for sending
       CustomMessage *msg = new CustomMessage("DataMessage");

       char header = static_cast<char>(seqNum + i);
       char trailer = calculateParity(framedPayload);

       msg->setM_Header(header);
       msg->setM_Payload(framedPayload.c_str()); // Set the framed payload
       msg->setM_Trailer(trailer);
       msg->setM_Type(0); // Type for data messages (could be 0, 1 for ACK, 2 for NACK)

        std::string errorCode = message.substr(0, 4);
        simulateErrors(msg, errorCode);

        logEvent("Sending frame with SeqNum=" + std::to_string(seqNum + i));
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
    logEvent("Timeout occurred. Retransmitting frames...");
    sendFrames();
}

void Node::receiveFrame(cMessage *msg)
{
    CustomMessage *dataMsg = check_and_cast<CustomMessage *>(msg);
    int seqNum = dataMsg->getM_Header(); // Sequence number from the header
    std::string payload = dataMsg->getM_Payload();
    char trailer = dataMsg->getM_Trailer();
    std::string crc = computeCRC(payload);

    if (checkCRC(payload, crc)) {
        std::string originalPayload = byteUnstuff(payload);
        logEvent("Received correct frame: SeqNum=" + std::to_string(seqNum) +
                 ", Payload=" + originalPayload);
        // Send ACK
        CustomMessage *ack = new CustomMessage("ACK");
        ack->setM_Header(seqNum); // Acknowledge the received sequence number
        ack->setM_Type(1); // Set type to ACK
        send(ack, "out");
    } else {
        logEvent("Frame corrupted: SeqNum=" + std::to_string(seqNum));
        // Send NACK
        CustomMessage *nack = new CustomMessage("NACK");
        nack->setM_Header(seqNum); // NACK for the corrupted frame
        nack->setM_Type(2); // Set type to NACK
        send(nack, "out");
    }
}


 std::string Node::byteStuff(const std::string &payload) {
     std::ostringstream framed;
     framed << '$'; // Start flag
     for (char c : payload) {
         if (c == '$' || c == '/') {
             framed << '/'; // Escape special characters
         }
         framed << c;
     }
     framed << '$'; // End flag
     return framed.str();
 }

 std::string Node::byteUnstuff(const std::string &framed) {
     std::ostringstream payload;
     bool escape = false;
     for (size_t i = 1; i < framed.size() - 1; ++i) { // Skip start and end flags
         if (framed[i] == '/') {
             escape = true;
             continue;
         }
         payload << framed[i];
         escape = false;
     }
     return payload.str();
 }

 std::string Node::computeCRC(const std::string &data) {
     const std::string generator = "1101"; // Example CRC generator (can be adjusted)
     std::string augmentedData = data + std::string(generator.size() - 1, '0');
     std::string remainder = augmentedData.substr(0, generator.size());

     for (size_t i = generator.size(); i < augmentedData.size(); ++i) {
         if (remainder[0] == '1') {
             for (size_t j = 0; j < generator.size(); ++j) {
                 remainder[j] = (remainder[j] == generator[j]) ? '0' : '1';
             }
         }
         remainder = remainder.substr(1) + augmentedData[i];
     }

     return remainder.substr(0, generator.size() - 1);
 }

 bool Node::checkCRC(const std::string &data, const std::string &crc) {
     return computeCRC(data) == crc;
 }

 void Node::simulateErrors(CustomMessage *frame, const std::string &errorCode)
 {
     std::string payload = frame->getM_Payload(); // Retrieve payload from CustomMessage

     // Modification
     if (errorCode[0] == '1') {
         int bitToFlip = intuniform(0, payload.size() * 8 - 1); // Select a random bit to flip
         int charIndex = bitToFlip / 8;
         int bitInChar = bitToFlip % 8;

         payload[charIndex] ^= (1 << bitInChar); // Flip the chosen bit
         frame->setM_Payload(payload.c_str());      // Update the modified payload

         EV << "Modified bit " << bitToFlip << " in payload.\n";
     }

     // Loss
     if (errorCode[1] == '1') {
         EV << "Frame lost due to error code.\n";
         delete frame; // Delete the frame to simulate loss
         return;
     }

     // Duplication
     if (errorCode[2] == '1') {
         CustomMessage *dupFrame = frame->dup();       // Duplicate the frame
         sendDelayed(dupFrame, par("duplicationDelay").doubleValue(), "out");
         EV << "Duplicated frame sent with delay.\n";
     }

     // Delay
     if (errorCode[3] == '1') {
         sendDelayed(frame, par("errorDelay").doubleValue(), "out");
         EV << "Frame delayed due to error code.\n";
     } else {
         send(frame, "out"); // Send the frame normally
         EV << "Frame sent without delay.\n";
     }
 }

 char Node::calculateParity(const std::string& payload)
 {
     char parity = 0;
     for (char c : payload) {
         parity ^= c; // XOR all the characters to calculate parity
     }
     return parity;
 }
 void Node::logEvent(const std::string &event) {
     std::ofstream logFile("output.txt", std::ios::app);
     if (logFile.is_open()) {
         logFile << "[" << simTime() << "] " << event << "\n";
         logFile.close();
     } else {
         EV << "Error opening log file!\n";
     }
 }
