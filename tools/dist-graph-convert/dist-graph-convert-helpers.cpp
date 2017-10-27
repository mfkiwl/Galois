/** Distributed graph converter helpers -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2017, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * Distributed graph converter helper implementations.
 *
 * @author Loc Hoang <l_hoang@utexas.edu>
 */
#include "dist-graph-convert-helpers.h"

void MPICheck(int errcode) {
  if (errcode != MPI_SUCCESS) {
    MPI_Abort(MPI_COMM_WORLD, errcode);
  }
}

std::vector<std::pair<uint64_t, uint64_t>> getHostToNodeMapping(
    const uint64_t numHosts, const uint64_t totalNumNodes
) {
  GALOIS_ASSERT((totalNumNodes != 0), "host2node mapping needs numNodes");

  std::vector<std::pair<uint64_t, uint64_t>> hostToNodes;

  for (unsigned i = 0; i < numHosts; i++) {
    hostToNodes.emplace_back(
      galois::block_range((uint64_t)0, (uint64_t)totalNumNodes, i, numHosts)
    );
  }

  return hostToNodes;
}

uint32_t findHostID(const uint64_t gID, 
            const std::vector<std::pair<uint64_t, uint64_t>> hostToNodes) {
  for (uint64_t host = 0; host < hostToNodes.size(); host++) {
    if (gID >= hostToNodes[host].first && gID < hostToNodes[host].second) {
      return host;
    }
  }
  return -1;
}

uint64_t getFileSize(std::ifstream& openFile) {
  openFile.seekg(0, std::ios_base::end);
  return openFile.tellg();
}

std::pair<uint64_t, uint64_t> determineByteRange(std::ifstream& edgeListFile,
                                                 uint64_t fileSize) {
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;
  uint64_t totalNumHosts = net.Num;

  uint64_t initialStart;
  uint64_t initialEnd;
  std::tie(initialStart, initialEnd) = galois::block_range((uint64_t)0, 
                                                           (uint64_t)fileSize,
                                                           hostID, 
                                                           totalNumHosts);

  //printf("[%lu] Initial byte %lu to %lu\n", hostID, initialStart, initialEnd);

  bool startGood = false;
  if (initialStart != 0) {
    // good starting point if the prev char was a new line (i.e. this start
    // location is the beginning of a line)
    // TODO factor this out
    edgeListFile.seekg(initialStart - 1);
    char testChar = edgeListFile.get();
    if (testChar == '\n') {
      startGood = true;
    }
  } else {
    // start is 0; perfect starting point, need no adjustment
    startGood = true;
  }

  bool endGood = false;
  if (initialEnd != fileSize && initialEnd != 0) {
    // good end point if the prev char was a new line (i.e. this end
    // location is the beginning of a line; recall non-inclusive)
    // TODO factor this out
    edgeListFile.seekg(initialEnd - 1);
    char testChar = edgeListFile.get();
    if (testChar == '\n') {
      endGood = true;
    }
  } else {
    endGood = true;
  }

  uint64_t finalStart = initialStart;
  if (!startGood) {
    // find next new line
    // TODO factor this out
    edgeListFile.seekg(initialStart);
    std::string dummy;
    std::getline(edgeListFile, dummy);
    finalStart = edgeListFile.tellg();
  }

  uint64_t finalEnd = initialEnd;
  if (!endGood) {
    // find next new line
    // TODO factor out
    edgeListFile.seekg(initialEnd);
    std::string dummy;
    std::getline(edgeListFile, dummy);
    finalEnd = edgeListFile.tellg();
  }

  return std::pair<uint64_t, uint64_t>(finalStart, finalEnd);
}

void sendEdgeCounts(
    const std::vector<std::pair<uint64_t, uint64_t>>& hostToNodes,
    uint64_t localNumEdges, const std::vector<uint32_t>& localEdges
) {
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;
  uint64_t totalNumHosts = net.Num;

  printf("[%lu] Determinining edge counts\n", hostID);

  //std::vector<galois::GAccumulator<uint64_t>> numEdgesPerHost(totalNumHosts);
  std::vector<uint64_t> numEdgesPerHost(totalNumHosts);

  //// determine to which host each edge will go
  //galois::do_all(
  //  galois::iterate((uint64_t)0, localNumEdges),
  //  [&] (uint64_t edgeIndex) {
  //    uint32_t src = localEdges[edgeIndex * 2];
  //    uint32_t edgeOwner = findHostID(src, hostToNodes);
  //    numEdgesPerHost[edgeOwner] += 1;
  //  },
  //  galois::loopname("EdgeInspection"),
  //  galois::no_stats(),
  //  galois::steal<false>(),
  //  galois::timeit()
  //);

  for (uint64_t edgeIndex = 0; edgeIndex < localNumEdges; edgeIndex++) {
    uint32_t src = localEdges[edgeIndex * 2];
    uint32_t edgeOwner = findHostID(src, hostToNodes);
    numEdgesPerHost[edgeOwner] += 1;
  }

  printf("[%lu] Sending edge counts\n", hostID);

  // tell hosts how many edges they should expect
  std::vector<uint64_t> edgeVectorToSend;
  for (unsigned h = 0; h < totalNumHosts; h++) {
    if (h == hostID) continue;
    galois::runtime::SendBuffer b;
    galois::runtime::gSerialize(b, numEdgesPerHost[h]);
    net.sendTagged(h, galois::runtime::evilPhase, b);
  }
}

uint64_t receiveEdgeCounts() {
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;
  uint64_t totalNumHosts = net.Num;

  printf("[%lu] Receiving edge counts\n", hostID);

  uint64_t edgesToReceive = 0;

  // receive
  for (unsigned h = 0; h < totalNumHosts; h++) {
    if (h == hostID) continue;
    decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) rBuffer;

    uint64_t recvCount;

    do {
      rBuffer = net.recieveTagged(galois::runtime::evilPhase, nullptr);
    } while (!rBuffer);
    galois::runtime::gDeserialize(rBuffer->second, recvCount);

    edgesToReceive += recvCount;
  }

  galois::runtime::evilPhase++;

  return edgesToReceive;
}

// TODO make implementation smaller/cleaner i.e. refactor
void sendAssignedEdges(
    const std::vector<std::pair<uint64_t, uint64_t>>& hostToNodes,
    uint64_t localNumEdges, const std::vector<uint32_t>& localEdges,
    std::vector<std::vector<uint32_t>>& localSrcToDest,
    std::vector<std::mutex>& nodeLocks)
{
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;
  uint64_t totalNumHosts = net.Num;

  printf("[%lu] Going to send assigned edges\n", hostID);

  using DestVectorTy = std::vector<std::vector<uint32_t>>;
  galois::substrate::PerThreadStorage<DestVectorTy> 
      dstVectors(totalNumHosts);
  using SendBufferVectorTy = std::vector<galois::runtime::SendBuffer>;
  galois::substrate::PerThreadStorage<SendBufferVectorTy> 
      sendBuffers(totalNumHosts);
  galois::substrate::PerThreadStorage<std::vector<uint64_t>> 
      lastSourceSentStorage(totalNumHosts);

  // initialize last source sent
  galois::on_each(
    [&] (unsigned tid, unsigned nthreads) {
      for (unsigned h = 0; h < totalNumHosts; h++) {
        (*(lastSourceSentStorage.getLocal()))[h] = 0;
      }
    },
    galois::no_stats()
  );

  printf("[%lu] Passing through edges and assigning\n", hostID);

  // pass 1: determine to which host each edge will go
  galois::do_all(
    galois::iterate((uint64_t)0, localNumEdges),
    [&] (uint64_t edgeIndex) {
      uint32_t src = localEdges[edgeIndex * 2];
      uint32_t edgeOwner = findHostID(src, hostToNodes);
      uint32_t dst = localEdges[(edgeIndex * 2) + 1];
      uint32_t localID = src - hostToNodes[edgeOwner].first;

      if (edgeOwner != hostID) {
        // send off to correct host
        auto& hostSendBuffer = (*(sendBuffers.getLocal()))[edgeOwner];
        auto& dstVector = (*(dstVectors.getLocal()))[edgeOwner];
        auto& lastSourceSent = 
            (*(lastSourceSentStorage.getLocal()))[edgeOwner];

        if (lastSourceSent == localID) {
          dstVector.emplace_back(dst);
        } else {
          // serialize vector if anything exists in it + send buffer if
          // reached some limit
          if (dstVector.size() > 0) {
            uint64_t globalSourceID = lastSourceSent + 
                                      hostToNodes[edgeOwner].first;
            galois::runtime::gSerialize(hostSendBuffer, globalSourceID, 
                                        dstVector);
            dstVector.clear();
            if (hostSendBuffer.size() > 1400) {
              net.sendTagged(edgeOwner, galois::runtime::evilPhase, 
                             hostSendBuffer);
              hostSendBuffer.getVec().clear();
            }
          }

          dstVector.emplace_back(dst);
          lastSourceSent = localID;
        }
      } else {
        // save to edge dest array
        nodeLocks[localID].lock();
        localSrcToDest[localID].emplace_back(dst);
        nodeLocks[localID].unlock();
      }
    },
    galois::loopname("Pass2"),
    galois::no_stats(),
    galois::steal<false>(),
    galois::timeit()
  );

  printf("[%lu] Buffer cleanup\n", hostID);

  // cleanup: each thread serialize + send out remaining stuff
  galois::on_each(
    [&] (unsigned tid, unsigned nthreads) {
      for (unsigned h = 0; h < totalNumHosts; h++) {
        if (h == hostID) continue;
        auto& hostSendBuffer = (*(sendBuffers.getLocal()))[h];
        auto& dstVector = (*(dstVectors.getLocal()))[h];
        uint64_t lastSourceSent = (*(lastSourceSentStorage.getLocal()))[h];

        if (dstVector.size() > 0) {
          uint64_t globalSourceID = lastSourceSent + 
                                    hostToNodes[h].first;
          galois::runtime::gSerialize(hostSendBuffer, globalSourceID,
                                      dstVector);
          dstVector.clear();
        }

        if (hostSendBuffer.size() > 0) {
          net.sendTagged(h, galois::runtime::evilPhase, hostSendBuffer);
          hostSendBuffer.getVec().clear();
        }
      }
    },
    galois::loopname("Pass2Cleanup"),
    galois::timeit(),
    galois::no_stats()
  );
}

void receiveAssignedEdges(std::atomic<uint64_t>& edgesToReceive,
    const std::vector<std::pair<uint64_t, uint64_t>>& hostToNodes,
    std::vector<std::vector<uint32_t>>& localSrcToDest,
    std::vector<std::mutex>& nodeLocks)
{
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;

  printf("[%lu] Going to receive assigned edges\n", hostID);

  // receive edges
  galois::on_each(
    [&] (unsigned tid, unsigned nthreads) {
      std::vector<uint32_t> recvVector;
      while (edgesToReceive) {
        decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) 
            rBuffer;
        rBuffer = net.recieveTagged(galois::runtime::evilPhase, nullptr);
        
        if (rBuffer) {
          auto& receiveBuffer = rBuffer->second;
          while (receiveBuffer.r_size() > 0) {
            uint64_t src;
            galois::runtime::gDeserialize(receiveBuffer, src, recvVector);
            edgesToReceive -= recvVector.size();
            GALOIS_ASSERT(findHostID(src, hostToNodes) == hostID);
            uint32_t localID = src - hostToNodes[hostID].first;

            nodeLocks[localID].lock();
            for (unsigned i = 0; i < recvVector.size(); i++) {
              localSrcToDest[localID].emplace_back(recvVector[i]);
            }
            nodeLocks[localID].unlock();
          }
        }
      }
    },
    galois::loopname("EdgeReceiving"),
    galois::timeit(),
    galois::no_stats()
  );
  galois::runtime::evilPhase++; 

  printf("[%lu] Receive assigned edges finished\n", hostID);
}

std::vector<uint64_t> getEdgesPerHost(uint64_t localAssignedEdges) {
  auto& net = galois::runtime::getSystemNetworkInterface();
  uint64_t hostID = net.ID;
  uint64_t totalNumHosts = net.Num;

  printf("[%lu] Informing other hosts about number of edges\n", hostID);

  std::vector<uint64_t> edgesPerHost(totalNumHosts);

  for (unsigned h = 0; h < totalNumHosts; h++) {
    if (h == hostID) continue;
    galois::runtime::SendBuffer b;
    galois::runtime::gSerialize(b, localAssignedEdges);
    net.sendTagged(h, galois::runtime::evilPhase, b);
  }

  // receive
  for (unsigned h = 0; h < totalNumHosts; h++) {
    if (h == hostID) {
      edgesPerHost[h] = localAssignedEdges;
      continue;
    }

    decltype(net.recieveTagged(galois::runtime::evilPhase, nullptr)) rBuffer;
    uint64_t otherAssignedEdges;
    do {
      rBuffer = net.recieveTagged(galois::runtime::evilPhase, nullptr);
    } while (!rBuffer);
    galois::runtime::gDeserialize(rBuffer->second, otherAssignedEdges);

    edgesPerHost[rBuffer->first] = otherAssignedEdges;
  }
  galois::runtime::evilPhase++; 

  return edgesPerHost;
}

void writeGrHeader(MPI_File& gr, uint64_t version, uint64_t sizeOfEdge,
                   uint64_t totalNumNodes, uint64_t totalEdgeCount) {
  // I won't check status here because there should be no reason why 
  // writing 8 bytes per write would fail.... (I hope at least)
  MPICheck(MPI_File_write_at(gr, 0, &version, 1, MPI_UINT64_T, 
           MPI_STATUS_IGNORE));
  MPICheck(MPI_File_write_at(gr, sizeof(uint64_t), &sizeOfEdge, 1, 
                             MPI_UINT64_T, MPI_STATUS_IGNORE));
  MPICheck(MPI_File_write_at(gr, sizeof(uint64_t) * 2, &totalNumNodes, 1, 
                             MPI_UINT64_T, MPI_STATUS_IGNORE));
  MPICheck(MPI_File_write_at(gr, sizeof(uint64_t) * 3, &totalEdgeCount, 1, 
                             MPI_UINT64_T, MPI_STATUS_IGNORE));
}

void writeNodeIndexData(MPI_File& gr, uint64_t nodesToWrite, 
                        uint64_t nodeIndexOffset, 
                        std::vector<uint64_t>& edgePrefixSum) {
  MPI_Status writeStatus;
  while (nodesToWrite != 0) {
    MPICheck(MPI_File_write_at(gr, nodeIndexOffset, edgePrefixSum.data(),
                               nodesToWrite, MPI_UINT64_T, &writeStatus));
    
    int itemsWritten;
    MPI_Get_count(&writeStatus, MPI_UINT64_T, &itemsWritten);
    nodesToWrite -= itemsWritten;
    nodeIndexOffset += itemsWritten * sizeof(uint64_t);
  }
}

void writeEdgeDestData(MPI_File& gr, uint64_t localNumNodes, 
                       uint64_t edgeDestOffset,
                       std::vector<std::vector<uint32_t>>& localSrcToDest) {
  MPI_Status writeStatus;

  for (unsigned i = 0; i < localNumNodes; i++) {
    std::vector<uint32_t> currentDests = localSrcToDest[i];
    uint64_t numToWrite = currentDests.size();

    while (numToWrite != 0) {
      MPICheck(MPI_File_write_at(gr, edgeDestOffset, currentDests.data(),
                                 numToWrite, MPI_UINT32_T, &writeStatus));

      int itemsWritten;
      MPI_Get_count(&writeStatus, MPI_UINT32_T, &itemsWritten);
      numToWrite -= itemsWritten;
      edgeDestOffset += sizeof(uint32_t) * itemsWritten;
    }
  }
}