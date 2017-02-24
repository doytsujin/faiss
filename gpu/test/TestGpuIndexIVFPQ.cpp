
/**
 * Copyright (c) 2015-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the CC-by-NC license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Copyright 2004-present Facebook. All Rights Reserved.

#include "../../IndexFlat.h"
#include "../../IndexIVFPQ.h"
#include "../GpuIndexIVFPQ.h"
#include "../StandardGpuResources.h"
#include "../utils/DeviceUtils.h"
#include "../test/TestUtils.h"
#include <cmath>
#include <gtest/gtest.h>
#include <sstream>
#include <vector>


void pickEncoding(int& codes, int& dim) {
  std::vector<int> codeSizes{
    3, 4, 8, 12, 16, 20, 24,
      28, 32, 40, 48, 56, 64, 96
  };

  std::vector<int> dimSizes{4, 8, 16, 32};

  codes = codeSizes[faiss::gpu::randVal(0, codeSizes.size() - 1)];

  while (true) {
    dim = codes * dimSizes[faiss::gpu::randVal(0, dimSizes.size() - 1)];

    // for such a small test, super-low or high dim is more likely to
    // generate comparison errors
    if (dim < 512 && dim >= 64) {
      return;
    }
  }
}

struct Options {
  Options() {
    numAdd = faiss::gpu::randVal(10000, 30000);
    numCentroids = std::sqrt((float) numAdd);
    numTrain = numCentroids * 40;

    pickEncoding(codes, dim);

    bitsPerCode = faiss::gpu::randVal(3, 8);
    nprobe = std::min(faiss::gpu::randVal(40, 1000), numCentroids);
    numQuery = faiss::gpu::randVal(32, 256);
    k = std::min(faiss::gpu::randVal(10, 50), numAdd / 40);
    usePrecomputed = faiss::gpu::randBool();
    indicesOpt = faiss::gpu::randSelect({
        faiss::gpu::INDICES_CPU,
          faiss::gpu::INDICES_32_BIT,
          faiss::gpu::INDICES_64_BIT});
    if (codes > 48) {
      // large codes can only fit using float16
      useFloat16 = true;
    } else {
      useFloat16 = faiss::gpu::randBool();
    }

    device = faiss::gpu::randVal(0, faiss::gpu::getNumDevices() - 1);
  }

  std::string toString() const {
    std::stringstream str;
    str << "IVFPQ device " << device
        << " numVecs " << numAdd
        << " dim " << dim
        << " numCentroids " << numCentroids
        << " codes " << codes
        << " bitsPerCode " << bitsPerCode
        << " nprobe " << nprobe
        << " numQuery " << numQuery
        << " k " << k
        << " usePrecomputed " << usePrecomputed
        << " indicesOpt " << indicesOpt
        << " useFloat16 " << useFloat16;

    return str.str();
  }

  float getCompareEpsilon() const {
    return 0.015f;
  }

  float getPctMaxDiff1() const {
    return useFloat16 ? 0.30f : 0.10f;
  }

  float getPctMaxDiffN() const {
    return useFloat16 ? 0.05f : 0.015f;
  }

  int numAdd;
  int numCentroids;
  int numTrain;
  int codes;
  int dim;
  int bitsPerCode;
  int nprobe;
  int numQuery;
  int k;
  bool usePrecomputed;
  faiss::gpu::IndicesOptions indicesOpt;
  bool useFloat16;
  int device;
};

TEST(TestGpuIndexIVFPQ, Query) {
  for (int tries = 0; tries < 5; ++tries) {
    faiss::gpu::newTestSeed();

    Options opt;

    std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
    std::vector<float> addVecs = faiss::gpu::randVecs(opt.numAdd, opt.dim);

    faiss::IndexFlatL2 coarseQuantizer(opt.dim);
    faiss::IndexIVFPQ cpuIndex(&coarseQuantizer, opt.dim, opt.numCentroids,
                               opt.codes, opt.bitsPerCode);
    cpuIndex.nprobe = opt.nprobe;
    cpuIndex.train(opt.numTrain, trainVecs.data());
    cpuIndex.add(opt.numAdd, addVecs.data());

    faiss::gpu::StandardGpuResources res;
    res.noTempMemory();

    faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                       opt.indicesOpt,
                                       opt.useFloat16,
                                       &cpuIndex);
    gpuIndex.setPrecomputedCodes(opt.usePrecomputed);
    gpuIndex.setNumProbes(opt.nprobe);

    faiss::gpu::compareIndices(cpuIndex, gpuIndex,
                               opt.numQuery, opt.dim, opt.k, opt.toString(),
                               opt.getCompareEpsilon(),
                               opt.getPctMaxDiff1(),
                               opt.getPctMaxDiffN());
  }
}

TEST(TestGpuIndexIVFPQ, Add) {
  for (int tries = 0; tries < 5; ++tries) {
    faiss::gpu::newTestSeed();

    Options opt;

    std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
    std::vector<float> addVecs = faiss::gpu::randVecs(opt.numAdd, opt.dim);

    faiss::IndexFlatL2 coarseQuantizer(opt.dim);
    faiss::IndexIVFPQ cpuIndex(&coarseQuantizer, opt.dim, opt.numCentroids,
                               opt.codes, opt.bitsPerCode);
    cpuIndex.nprobe = opt.nprobe;
    cpuIndex.train(opt.numTrain, trainVecs.data());

    faiss::gpu::StandardGpuResources res;
    res.noTempMemory();

    faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                       opt.indicesOpt,
                                       opt.useFloat16,
                                       &cpuIndex);
    gpuIndex.setPrecomputedCodes(opt.usePrecomputed);
    gpuIndex.setNumProbes(opt.nprobe);

    gpuIndex.add(opt.numAdd, addVecs.data());
    cpuIndex.add(opt.numAdd, addVecs.data());

    faiss::gpu::compareIndices(cpuIndex, gpuIndex,
                               opt.numQuery, opt.dim, opt.k, opt.toString(),
                               opt.getCompareEpsilon(),
                               opt.getPctMaxDiff1(),
                               opt.getPctMaxDiffN());
  }
}

TEST(TestGpuIndexIVFPQ, CopyTo) {
  faiss::gpu::newTestSeed();

  Options opt;
  std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
  std::vector<float> addVecs = faiss::gpu::randVecs(opt.numAdd, opt.dim);

  faiss::gpu::StandardGpuResources res;
  res.noTempMemory();

  faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                     opt.dim,
                                     opt.numCentroids,
                                     opt.codes,
                                     opt.bitsPerCode,
                                     opt.usePrecomputed,
                                     opt.indicesOpt,
                                     opt.useFloat16,
                                     faiss::METRIC_L2);
  gpuIndex.setNumProbes(opt.nprobe);
  gpuIndex.train(opt.numTrain, trainVecs.data());
  gpuIndex.add(opt.numAdd, addVecs.data());

  // Use garbage values to see if we overwrite them
  faiss::IndexFlatL2 cpuQuantizer(1);
  faiss::IndexIVFPQ cpuIndex(&cpuQuantizer, 1, 1, 1, 1);

  gpuIndex.copyTo(&cpuIndex);

  EXPECT_EQ(cpuIndex.ntotal, gpuIndex.ntotal);
  EXPECT_EQ(gpuIndex.ntotal, opt.numAdd);

  EXPECT_EQ(cpuIndex.d, gpuIndex.d);
  EXPECT_EQ(cpuIndex.d, opt.dim);
  EXPECT_EQ(cpuIndex.nlist, gpuIndex.getNumLists());
  EXPECT_EQ(cpuIndex.nprobe, gpuIndex.getNumProbes());
  EXPECT_EQ(cpuIndex.pq.M, gpuIndex.getNumSubQuantizers());
  EXPECT_EQ(gpuIndex.getNumSubQuantizers(), opt.codes);
  EXPECT_EQ(cpuIndex.pq.nbits, gpuIndex.getBitsPerCode());
  EXPECT_EQ(gpuIndex.getBitsPerCode(), opt.bitsPerCode);

  // Query both objects; results should be equivalent
  faiss::gpu::compareIndices(cpuIndex, gpuIndex,
                             opt.numQuery, opt.dim, opt.k, opt.toString(),
                             opt.getCompareEpsilon(),
                             opt.getPctMaxDiff1(),
                             opt.getPctMaxDiffN());
}

TEST(TestGpuIndexIVFPQ, CopyFrom) {
  faiss::gpu::newTestSeed();

  Options opt;
  std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
  std::vector<float> addVecs = faiss::gpu::randVecs(opt.numAdd, opt.dim);

  faiss::IndexFlatL2 coarseQuantizer(opt.dim);
  faiss::IndexIVFPQ cpuIndex(&coarseQuantizer, opt.dim, opt.numCentroids,
                             opt.codes, opt.bitsPerCode);
  cpuIndex.nprobe = opt.nprobe;
  cpuIndex.train(opt.numTrain, trainVecs.data());
  cpuIndex.add(opt.numAdd, addVecs.data());

  // Use garbage values to see if we overwrite them
  faiss::gpu::StandardGpuResources res;
  res.noTempMemory();

  faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                     1, 1, 1, 1, false, opt.indicesOpt,
                                     opt.useFloat16, faiss::METRIC_L2);
  gpuIndex.setNumProbes(1);

  gpuIndex.copyFrom(&cpuIndex);

  // Make sure we are equivalent
  EXPECT_EQ(cpuIndex.ntotal, gpuIndex.ntotal);
  EXPECT_EQ(gpuIndex.ntotal, opt.numAdd);

  EXPECT_EQ(cpuIndex.d, gpuIndex.d);
  EXPECT_EQ(cpuIndex.d, opt.dim);
  EXPECT_EQ(cpuIndex.nlist, gpuIndex.getNumLists());
  EXPECT_EQ(cpuIndex.nprobe, gpuIndex.getNumProbes());
  EXPECT_EQ(cpuIndex.pq.M, gpuIndex.getNumSubQuantizers());
  EXPECT_EQ(gpuIndex.getNumSubQuantizers(), opt.codes);
  EXPECT_EQ(cpuIndex.pq.nbits, gpuIndex.getBitsPerCode());
  EXPECT_EQ(gpuIndex.getBitsPerCode(), opt.bitsPerCode);

  // Query both objects; results should be equivalent
  faiss::gpu::compareIndices(cpuIndex, gpuIndex,
                             opt.numQuery, opt.dim, opt.k, opt.toString(),
                             opt.getCompareEpsilon(),
                             opt.getPctMaxDiff1(),
                             opt.getPctMaxDiffN());
}

TEST(TestGpuIndexIVFPQ, QueryNaN) {
  faiss::gpu::newTestSeed();

  Options opt;

  std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
  std::vector<float> addVecs = faiss::gpu::randVecs(opt.numAdd, opt.dim);

  faiss::gpu::StandardGpuResources res;
  res.noTempMemory();

  faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                     opt.dim,
                                     opt.numCentroids,
                                     opt.codes,
                                     opt.bitsPerCode,
                                     opt.usePrecomputed,
                                     opt.indicesOpt,
                                     opt.useFloat16,
                                     faiss::METRIC_L2);

  gpuIndex.setPrecomputedCodes(opt.usePrecomputed);
  gpuIndex.setNumProbes(opt.nprobe);

  gpuIndex.train(opt.numTrain, trainVecs.data());
  gpuIndex.add(opt.numAdd, addVecs.data());

  int numQuery = 10;
  std::vector<float> nans(numQuery * opt.dim,
                          std::numeric_limits<float>::quiet_NaN());

  std::vector<float> distances(numQuery * opt.k, 0);
  std::vector<faiss::Index::idx_t> indices(numQuery * opt.k, 0);

  gpuIndex.search(numQuery,
                  nans.data(),
                  opt.k,
                  distances.data(),
                  indices.data());

  for (int q = 0; q < numQuery; ++q) {
    for (int k = 0; k < opt.k; ++k) {
      EXPECT_EQ(indices[q * opt.k + k], -1);
      EXPECT_EQ(distances[q * opt.k + k], std::numeric_limits<float>::max());
    }
  }
}

TEST(TestGpuIndexIVFPQ, AddNaN) {
  faiss::gpu::newTestSeed();

  Options opt;

  faiss::gpu::StandardGpuResources res;
  res.noTempMemory();

  faiss::gpu::GpuIndexIVFPQ gpuIndex(&res, opt.device,
                                     opt.dim,
                                     opt.numCentroids,
                                     opt.codes,
                                     opt.bitsPerCode,
                                     opt.usePrecomputed,
                                     opt.indicesOpt,
                                     opt.useFloat16,
                                     faiss::METRIC_L2);

  gpuIndex.setPrecomputedCodes(opt.usePrecomputed);
  gpuIndex.setNumProbes(opt.nprobe);

  int numNans = 10;
  std::vector<float> nans(numNans * opt.dim,
                          std::numeric_limits<float>::quiet_NaN());

  // Make one vector valid, which should actually add
  for (int i = 0; i < opt.dim; ++i) {
    nans[i] = 0.0f;
  }

  std::vector<float> trainVecs = faiss::gpu::randVecs(opt.numTrain, opt.dim);
  gpuIndex.train(opt.numTrain, trainVecs.data());

  // should not crash
  EXPECT_EQ(gpuIndex.ntotal, 0);
  gpuIndex.add(numNans, nans.data());

  // Only the single valid vector should have added
  EXPECT_EQ(gpuIndex.ntotal, 1);

  std::vector<float> queryVecs = faiss::gpu::randVecs(opt.numQuery, opt.dim);
  std::vector<float> distance(opt.numQuery * opt.k, 0);
  std::vector<faiss::Index::idx_t> indices(opt.numQuery * opt.k, 0);

  // should not crash
  gpuIndex.search(opt.numQuery, queryVecs.data(), opt.k,
                  distance.data(), indices.data());

}
