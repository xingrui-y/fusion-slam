#include "Mapping.h"
#include "Constant.h"
#include "Reduction.h"
#include "RenderScene.h"

Mapping::Mapping() :
		meshUpdated(false), mapKeyUpdated(false), noKeysInMap(0) {
	create();
}

void Mapping::create() {

	// Reconstruction
	heapCounter.create(1);
	hashCounter.create(1);
	noVisibleEntries.create(1);
	heap.create(DeviceMap::NumSdfBlocks);
	sdfBlock.create(DeviceMap::NumVoxels);
	bucketMutex.create(DeviceMap::NumBuckets);
	hashEntries.create(DeviceMap::NumEntries);
	visibleEntries.create(DeviceMap::NumEntries);

	// Mesh Scene
	nBlocks.create(1);
	noTriangles.create(1);
	modelVertex.create(DeviceMap::MaxVertices);
	modelNormal.create(DeviceMap::MaxVertices);
	modelColor.create(DeviceMap::MaxVertices);
	blockPoses.create(DeviceMap::NumEntries);

	edgeTable.create(256);
	vertexTable.create(256);
	triangleTable.create(16, 256);
	edgeTable.upload(edgeTableHost);
	vertexTable.upload(vertexTableHost);
	triangleTable.upload(triangleTableHost);


	// Rendering
	zRangeMin.create(80, 60);
	zRangeMax.create(80, 60);
	noRenderingBlocks.create(1);
	renderingBlockList.create(DeviceMap::MaxRenderingBlocks);

	// Key Point
	mKeyMutex.create(KeyMap::MaxKeys);
	mORBKeys.create(KeyMap::maxEntries);
	tmpKeys.create(KeyMap::maxEntries);
	mapIndices.create(KeyMap::maxEntries);
	keyIndices.create(1500);

	reset();
}

void Mapping::createModel() {

	MeshScene(nBlocks, noTriangles, *this, edgeTable, vertexTable,
			triangleTable, modelNormal, modelVertex, modelColor, blockPoses);

	noTriangles.download(&noTrianglesHost);
	if (noTrianglesHost > 0) {
		meshUpdated = true;
	}
}

void Mapping::updateVisibility(Matrix3f Rview, Matrix3f RviewInv, float3 tview,
		float depthMin, float depthMax, float fx, float fy, float cx, float cy,
		uint & no) {

	CheckBlockVisibility(*this, noVisibleEntries, Rview, RviewInv, tview, 640,
			480, fx, fy, cx, cy, depthMax, depthMin, &no);
}

void Mapping::fuseColor(const DeviceArray2D<float> & depth,
		const DeviceArray2D<uchar3> & color, Matrix3f Rview, Matrix3f RviewInv,
		float3 tview, uint & no) {

	FuseMapColor(depth, color, noVisibleEntries, Rview, RviewInv, tview, *this,
			Frame::fx(0), Frame::fy(0), Frame::cx(0), Frame::cy(0),
			DeviceMap::DepthMax, DeviceMap::DepthMin, &no);

}

void Mapping::rayTrace(uint noVisibleBlocks, Matrix3f Rview, Matrix3f RviewInv,
		float3 tview, DeviceArray2D<float4> & vmap,	DeviceArray2D<float4> & nmap,
		float depthMin, float depthMax, float fx, float fy, float cx, float cy) {

	if (CreateRenderingBlocks(visibleEntries, zRangeMin, zRangeMax, depthMax, depthMin,
			renderingBlockList, noRenderingBlocks, RviewInv, tview,
			noVisibleBlocks, fx, fy, cx, cy)) {

		Raycast(*this, vmap, nmap, zRangeMin, zRangeMax, Rview, RviewInv, tview,
				1.0 / fx, 1.0 / fy, cx, cy);
	}
}

void Mapping::reset() {
	ResetMap(*this);
	ResetKeyPoints(*this);
}

//Mapping::operator KeyMap() {
//
//	KeyMap map;
//	map.Keys = mORBKeys;
//	map.Mutex = mKeyMutex;
//	return map;
//}

Mapping::operator KeyMap() const {

	KeyMap map;
	map.Keys = mORBKeys;
	map.Mutex = mKeyMutex;
	return map;
}

//Mapping::operator DeviceMap() {
//
//	DeviceMap map;
//	map.heapMem = heap;
//	map.heapCounter = heapCounter;
//	map.noVisibleBlocks = noVisibleEntries;
//	map.bucketMutex = bucketMutex;
//	map.hashEntries = hashEntries;
//	map.visibleEntries = visibleEntries;
//	map.voxelBlocks = sdfBlock;
//	map.entryPtr = hashCounter;
//	return map;
//}

Mapping::operator DeviceMap() const {

	DeviceMap map;
	map.heapMem = heap;
	map.heapCounter = heapCounter;
	map.noVisibleBlocks = noVisibleEntries;
	map.bucketMutex = bucketMutex;
	map.hashEntries = hashEntries;
	map.visibleEntries = visibleEntries;
	map.voxelBlocks = sdfBlock;
	map.entryPtr = hashCounter;
	return map;
}
