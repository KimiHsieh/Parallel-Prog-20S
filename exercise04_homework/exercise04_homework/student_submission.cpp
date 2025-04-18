#include <iostream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <getopt.h>
#include <omp.h>


#include "a-star-navigator.h"
#include "VideoOutput.h"
#include "Utility.h"

void simulate_waves(ProblemData &problemData, int t) {
    auto &islandMap = problemData.islandMap;
    float (&secondLastWaveIntensity)[MAP_SIZE][MAP_SIZE] = *problemData.secondLastWaveIntensity;
    float (&lastWaveIntensity)[MAP_SIZE][MAP_SIZE] = *problemData.lastWaveIntensity;
    float (&currentWaveIntensity)[MAP_SIZE][MAP_SIZE] = *problemData.currentWaveIntensity;

    #pragma omp parallel for collapse (2)
    for (int x = 1; x < MAP_SIZE - 1; ++x) {
        for (int y = 1; y < MAP_SIZE - 1; ++y) {

            /*
             * TODO@Students: This is the first part of the main calculation. Here, we simulate the waves on the
             * stormy seas. Look at all TODOS for parallelization.
             */

            // Simulate some waves

            // The acceleration is the relative difference between the current point and the last.
            float acceleration = lastWaveIntensity[x][y - 1]
                                 + lastWaveIntensity[x - 1][y]
                                 + lastWaveIntensity[x + 1][y]
                                 + lastWaveIntensity[x][y + 1]
                                 - 4 * lastWaveIntensity[x][y];

            // The acceleration is multiplied with an attack value, specifying how fast the system can accelerate.
            acceleration *= ATTACK_FACTOR;

            // The last_velocity is calculated from the difference between the last intensity and the
            // second to last intensity
            float last_velocity = lastWaveIntensity[x][y] - secondLastWaveIntensity[x][y];

            // energy preserved takes into account that storms lose energy to their environments over time. The
            // ratio of energy preserved is higher on open water, lower close to the shore and 0 on land.
            float energyPreserved = std::clamp(
                    ENERGY_PRESERVATION_FACTOR * (LAND_THRESHOLD - 0.1f * islandMap[x][y]),
                    0.0f, 1.0f);

            // There aren't any waves on land.
            if (islandMap[x][y] >= LAND_THRESHOLD) {
                currentWaveIntensity[x][y] = 0.0f;
            } else {
                currentWaveIntensity[x][y] =
                        std::clamp(lastWaveIntensity[x][y] + (last_velocity + acceleration) * energyPreserved,
                                   0.0f,
                                   1.0f);
            }
        }
    }
}

/*
 * Since all pirates like navigating by the stars, Captain Jack's favorite pathfinding algorithm is called A*.
 * Unfortunately, sometimes you just have to make do with what you have. So here we use a search algorithm that searches
 * the entire domain every time step and calculates all possible ship positions.
 */
bool findPathWithExhaustiveSearch(ProblemData &problemData, int timestep,
                                  std::vector<Position2D> &pathOutput) {
    auto &start = problemData.shipOrigin;
    auto &portRoyal = problemData.portRoyal;
    auto &islandMap = problemData.islandMap;
    auto &currentWaveIntensity = *problemData.currentWaveIntensity;
    auto &lastWaveIntensity = *problemData.lastWaveIntensity;

    /*
     * TODO@Students: This is the second part of the main calculation. Here, we find the shortest path to get back to
     * Port Royal without our ship capsizing. Take a look at whether this should be parallelized.
     */



    // The Jolly Mon (Jack's ship) is not very versatile. It can only move along the four cardinal directions by one
    // square each and along their diagonals. Alternatively, it can just try to stay where it is.
    Position2D neighbors[] = {
            Position2D(-1, 0),
            Position2D(0, -1),
            Position2D(1, 0),
            Position2D(0, 1),
            Position2D(-1, -1),
            Position2D(-1, 1),
            Position2D(1, -1),
            Position2D(1, 1),
            Position2D(0, 0)
    };

//    std::cerr << "Searching for position: " << portRoyal.x << " " << portRoyal.y << std::endl;
    int numPossiblePositions = 0;

    bool (&currentShipPositions)[MAP_SIZE][MAP_SIZE] = *problemData.currentShipPositions;
    bool (&previousShipPositions)[MAP_SIZE][MAP_SIZE] = *problemData.previousShipPositions;

    // We could always have been at the start in the previous frame since we get to choose when we start our journey.
    previousShipPositions[start.x][start.y] = true;

    // Ensure that our new buffer is set to zero. We need to ensure this because we are reusing previously used buffers.
    #pragma omp parallel for collapse (2)
    for (int x = 0; x < MAP_SIZE; ++x) {
        for (int y = 0; y < MAP_SIZE; ++y) {
            currentShipPositions[x][y] = false;
        }
    }

    // Do the actual path finding.
    bool flag = false; // Keypoint! Use flag!!!!

    #pragma omp parallel for schedule(dynamic,4)
    for (int x = 0; x < MAP_SIZE; ++x) {
        for (int y = 0; y < MAP_SIZE; ++y) {
            // If there is no possibility to reach this position then we don't need to process it.
            if (!previousShipPositions[x][y]) {
                continue;
            }
            Position2D previousPosition(x, y);


            // If we are not yet done then we have to take a look at our neighbors.
            for (Position2D &neighbor: neighbors) {
                // Get the neighboring position we are examining. It is one step further in time since we have to move
                // there first.
                Position2D neighborPosition = previousPosition + neighbor;

                // If position is out of bounds, skip it
                if (neighborPosition.x < 0 || neighborPosition.y < 0
                    || neighborPosition.x >= MAP_SIZE || neighborPosition.y >= MAP_SIZE) {
                    continue;
                }

                // If this position is already marked, skip it
                if (currentShipPositions[neighborPosition.x][neighborPosition.y]) {
                    continue;
                }

                // If we can't sail to this position because it is either on land or because the wave height is too
                // great for the Jolly Mon to handle, skip it
                if (islandMap[neighborPosition.x][neighborPosition.y] >= LAND_THRESHOLD ||
                    currentWaveIntensity[neighborPosition.x][neighborPosition.y] >= SHIP_THRESHOLD) {
                    continue;
                }

                // If we reach Port Royal, we win.
                if (neighborPosition == portRoyal) {

                    flag = true;
                }

                currentShipPositions[neighborPosition.x][neighborPosition.y] = true;
                numPossiblePositions++;
            }
        }
    }
    // This is not strictly required but can be used to track how much additional memory our path traceback structures
    // are using.
    problemData.numPredecessors += problemData.nodePredecessors[timestep].size();
//    std::cerr << "Possible positions: " << numPossiblePositions << " (" << problemData.numPredecessors
//              << " predecessors) " << std::endl;
    return flag;
}


/*
 * Your main simulation routine.
 */
int main(int argc, char *argv[]) {
    bool outputVisualization = false;
    bool constructPathForVisualization = false;
    int numProblems = 1;
    int option;
    while ((option = getopt(argc, argv, "vphn:")) != -1) {
        switch (option) {
            case 'v':
                outputVisualization = true;
                break;
            case 'p':
                constructPathForVisualization = true;
                break;
            case 'n':
                numProblems = strtol(optarg, nullptr, 0);
                if (numProblems <= 0) {
                    std::cerr << "Error parsing number problems." << std::endl;
                    exit(-1);
                }
                break;
            case 'h':
                std::cerr << "Usage: " << argv[0] << " [-v] [-p] [-n <numProblems>] [-h]" << std::endl
                          << "-v: Output a visualization to file out.mp4. Requires FFMPEG to be in your $PATH to work."
                          << std::endl
                          << "-p: Also output the actual path used to reach Port Royal to the visualization. Can be slow"
                             " and use lots of memory." << std::endl
                          << "-n: The number of problems to solve." << std::endl
                          << "-h: Show this help topic." << std::endl;
                exit(-1);
            default:
                std::cerr << "Unknown option: " << (unsigned char) option << std::endl;
                exit(-1);
        }
    }

    std::cerr << "Solving " << numProblems <<
              " problems (visualization: " << outputVisualization << ", path visualization "
              << constructPathForVisualization << ")" << std::endl;

    // Fetch the seed from our container host used to generate the problem. This starts the timer.
    unsigned int seed = Utility::readInput();

    // if (outputVisualization) {
    //     VideoOutput::beginVideoOutput();
    // }

    /*
     * TODO@Students: On the submission server, we are solving more than just one problem
     */
    for (int problem = 0; problem < numProblems; ++problem) {
        auto *problemData = new ProblemData();
        // problemData->outputVisualization = outputVisualization;
        // problemData->constructPathForVisualization = constructPathForVisualization;

        // Receive the problem from the system.
        Utility::generateProblem(seed + problem, *problemData);

        


        std::cerr << "Searching from ship position (" << problemData->shipOrigin.x << ", " << problemData->shipOrigin.y
                  << ") to Port Royal (" << problemData->portRoyal.x << ", " << problemData->portRoyal.y << ")."
                  << std::endl;

        int pathLength = -1;
        std::vector<Position2D> path;

        for (int t = 2; t < TIME_STEPS; t++) {
//        std::cerr << "Simulating storms" << std::endl;
            // First simulate all cycles of the storm
            simulate_waves(*problemData, t);

//        std::cerr << "Finding a path" << std::endl;
            // Help captain Sparrow navigate the waves
            if (findPathWithExhaustiveSearch(*problemData, t, path)) {
                // The length of the path is one shorter than the time step because the first frame is not part of the
                // pathfinding, and the second frame is always the start position.
                pathLength = t - 1;
            }

            // if (outputVisualization) {
            //     VideoOutput::writeVideoFrames(path, *problemData);
            // }
            if (pathLength != -1) {
                break;
            }

            // Rotates the buffers, recycling no longer needed data buffers for writing new data.
            problemData->flipSearchBuffers();
            problemData->flipWaveBuffers();
        }
        // Submit our solution back to the system.

        Utility::writeOutput(pathLength);

        
        // if (outputVisualization) {
        //     // Output the last frame some more times so that it's easier to see the path/result
        //     for (int i = 0; i < 30; ++i) {
        //         VideoOutput::writeVideoFrames(path, *problemData);
        //     }
        // }

        delete problemData;
    }
    // This stops the timer by printing DONE.
    Utility::stopTimer();

    // if (outputVisualization) {
    //     VideoOutput::endVideoOutput();
    // }
    return 0;
}
