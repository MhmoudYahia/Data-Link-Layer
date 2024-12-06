package datalinklayernet;

@license(LGPL);
network DataLinkLayerNet
{
    submodules:
        coordinator: Coordinator {
            parameters:
                inputFile = "coordinator.txt";  // Input file 
            @display("p=200,100");
        }
//        node0: Node {
//            parameters:
//                inputFile = "input0.txt";  // Input file for Node 0
//            @display("p=100,300");
//        }
        nodes[2]: Node;
//        {
//            parameters:
//                inputFile = "input1.txt";  // Input file for Node 1
//            @display("p=300,300");
//        }
    connections:
        coordinator.out[0] --> nodes[0].in[0];
        coordinator.out[1] --> nodes[1].in[0];
        nodes[0].out --> nodes[1].in[1]; // Node0 sends to Node1
        nodes[1].out --> nodes[0].in[1]; // Node1 sends to Node0
}
