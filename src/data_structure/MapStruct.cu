#include "MapStruct.h"

MapState state;
bool state_initialised = false;
__device__ MapState param;

class StateNotInitialised: public std::exception
{
public:
	const char * what() const throw ()
	{
		return "";
	}
};

class MemoryNotAllocated: public std::exception
{
public:
	const char * what() const throw ()
	{
		return "Device memory not properly allocated. \n"
						"Need to explicitly call allocate_device_memory()";
	}
};

__device__ __host__ int MapState::num_total_voxel() const
{
	return maxNumVoxelBlocks * BLOCK_SIZE3;
}

__device__ __host__ float MapState::block_size_metric() const
{
	return BLOCK_SIZE * voxel_size_;
}

__device__ __host__ int MapState::num_total_mesh_vertices() const
{
	return 3 * maxNumMeshTriangles;
}

__device__ __host__ float MapState::inverse_voxel_size() const
{
	return 1.0f / voxel_size_;
}

__device__ __host__ int MapState::num_excess_entry() const
{
	return maxNumHashEntries - maxNumBuckets;
}

__device__ __host__ float MapState::truncation_dist() const
{
	return 8.0f * voxel_size_;
}

__device__ __host__ float MapState::raycast_step_scale() const
{
	return 0.5 * truncation_dist() * inverse_voxel_size();
}

void update_device_map_state()
{
	safe_call(cudaMemcpyToSymbol(param, &state, sizeof(MapState)));

	if (!state_initialised)
		state_initialised = true;
}

__device__ HashEntry::HashEntry() :
				pos(make_int3(0)), next(-1), offset(-1)
{
}

__device__ HashEntry::HashEntry(int3 pos, int ptr, int offset) :
				pos(pos), next(ptr), offset(offset)
{
}

__device__ HashEntry::HashEntry(const HashEntry& other)
{
	pos = other.pos;
	next = other.next;
	offset = other.offset;
}

__device__ void HashEntry::release()
{
	next = -1;
}

__device__ void HashEntry::operator=(const HashEntry& other)
{
	pos = other.pos;
	next = other.next;
	offset = other.offset;
}

__device__ bool HashEntry::operator==(const int3& pos) const
				{
	return (this->pos == pos);
}

__device__ bool HashEntry::operator==(const HashEntry& other) const
				{
	return other.pos == pos;
}

__device__ Voxel::Voxel() :
				sdf_(std::nanf("0x7fffffff")), weight_(0)
{

}

__device__ Voxel::Voxel(float sdf, short weight, uchar3 rgb)
:
				sdf_(sdf), weight_(weight), rgb_(rgb)
{
}

__device__ void Voxel::release()
{
	sdf_ = std::nanf("0x7fffffff");
	weight_ = 0;
	rgb_ = make_uchar3(0);
}

__device__ void Voxel::getValue(float& sdf, uchar3& color) const
				{
	sdf = this->sdf_;
	color = this->rgb_;
}

__device__ void Voxel::operator=(const Voxel& other)
{
	sdf_ = other.sdf_;
	weight_ = other.weight_;
	rgb_ = other.rgb_;
}

__device__ uint MapStruct::Hash(const int3 &pos) const
				{
	int res = ((pos.x * 73856093) ^ (pos.y * 19349669) ^ (pos.z * 83492791)) % param.maxNumBuckets;
	if (res < 0)
		res += param.maxNumBuckets;

	return res;
}

__device__ HashEntry MapStruct::create_entry(const int3& pos, const int& offset)
{
	int old = atomicSub(heapCounter, 1);
	if (old >= 0)
	{
		int ptr = heapMem[old];
		if (ptr != -1)
			return HashEntry(pos, ptr * BLOCK_SIZE3, offset);
	}
	return HashEntry(pos, EntryAvailable, 0);
}

__device__ void MapStruct::CreateBlock(const int3& blockPos)
{
	int bucketId = Hash(blockPos);
	int* mutex = &bucketMutex[bucketId];
	HashEntry* e = &hashEntries[bucketId];
	HashEntry* eEmpty = nullptr;
	if (e->pos == blockPos && e->next != EntryAvailable)
		return;

	if (e->next == EntryAvailable && !eEmpty)
		eEmpty = e;

	while (e->offset > 0)
	{
		bucketId = param.maxNumBuckets + e->offset - 1;
		e = &hashEntries[bucketId];
		if (e->pos == blockPos && e->next != EntryAvailable)
			return;

		if (e->next == EntryAvailable && !eEmpty)
			eEmpty = e;
	}

	if (eEmpty)
	{
		int old = atomicExch(mutex, EntryOccupied);
		if (old == EntryAvailable)
		{
			*eEmpty = create_entry(blockPos, e->offset);
			atomicExch(mutex, EntryAvailable);
		}
	} else
	{
		int old = atomicExch(mutex, EntryOccupied);
		if (old == EntryAvailable)
		{
			int offset = atomicAdd(entryPtr, 1);
			if (offset <= param.num_excess_entry())
			{
				eEmpty = &hashEntries[param.maxNumBuckets + offset - 1];
				*eEmpty = create_entry(blockPos, 0);
				e->offset = offset;
			}
			atomicExch(mutex, EntryAvailable);
		}
	}
}

__device__ bool MapStruct::FindVoxel(const float3& pos, Voxel& vox)
{
	int3 voxel_pos = posWorldToVoxel(pos);
	return FindVoxel(voxel_pos, vox);
}

__device__ bool MapStruct::FindVoxel(const int3& pos, Voxel& vox)
{
	HashEntry entry = FindEntry(posVoxelToBlock(pos));
	if (entry.next == EntryAvailable)
		return false;
	int idx = posVoxelToIdx(pos);
	vox = voxelBlocks[entry.next + idx];
	return true;
}

__device__ void MapStruct::find_voxel(const int3 &voxel_pos, Voxel *&out) const
				{
	HashEntry *current;
	find_entry(posVoxelToBlock(voxel_pos), current);
	if (current != nullptr)
		out = &voxelBlocks[current->next + posVoxelToIdx(voxel_pos)];
}

__device__ Voxel MapStruct::FindVoxel(const int3& pos)
{
	HashEntry entry = FindEntry(posVoxelToBlock(pos));
	Voxel voxel;
	if (entry.next == EntryAvailable)
		return voxel;
	return voxelBlocks[entry.next + posVoxelToIdx(pos)];
}

__device__ Voxel MapStruct::FindVoxel(const float3& pos)
{
	int3 p = make_int3(pos);
	HashEntry entry = FindEntry(posVoxelToBlock(p));

	Voxel voxel;
	if (entry.next == EntryAvailable)
		return voxel;

	return voxelBlocks[entry.next + posVoxelToIdx(p)];
}

__device__ Voxel MapStruct::FindVoxel(const float3& pos, HashEntry& cache, bool& valid)
{
	int3 p = make_int3(pos);
	int3 blockPos = posVoxelToBlock(p);
	if (blockPos == cache.pos)
	{
		valid = true;
		return voxelBlocks[cache.next + posVoxelToIdx(p)];
	}

	HashEntry entry = FindEntry(blockPos);
	if (entry.next == EntryAvailable)
	{
		valid = false;
		return Voxel();
	}

	valid = true;
	cache = entry;
	return voxelBlocks[entry.next + posVoxelToIdx(p)];
}

__device__ HashEntry MapStruct::FindEntry(const float3& pos)
{
	int3 blockIdx = posWorldToBlock(pos);

	return FindEntry(blockIdx);
}

__device__ void MapStruct::find_entry(const int3 &block_pos, HashEntry *&out) const
				{
	uint bucket_idx = Hash(block_pos);
	out = &hashEntries[bucket_idx];
	if (out->next != EntryAvailable && out->pos == block_pos)
		return;

	while (out->offset > 0)
	{
		bucket_idx = param.maxNumBuckets + out->offset - 1;
		out = &hashEntries[bucket_idx];
		if (out->next != EntryAvailable && out->pos == block_pos)
			return;
	}

	out = nullptr;
}

__device__ HashEntry MapStruct::FindEntry(const int3& blockPos)
{
	uint bucketId = Hash(blockPos);
	HashEntry* e = &hashEntries[bucketId];
	if (e->next != EntryAvailable && e->pos == blockPos)
		return *e;

	while (e->offset > 0)
	{
		bucketId = param.maxNumBuckets + e->offset - 1;
		e = &hashEntries[bucketId];
		if (e->pos == blockPos && e->next != EntryAvailable)
			return *e;
	}
	return HashEntry(blockPos, EntryAvailable, 0);
}

__device__ int3 MapStruct::posWorldToVoxel(float3 pos) const
				{
	float3 p = pos / param.voxel_size_;
	return make_int3(p);
}

__device__ float3 MapStruct::posWorldToVoxelFloat(float3 pos) const
				{
	return pos / param.voxel_size_;
}

__device__ float3 MapStruct::posVoxelToWorld(int3 pos) const
				{
	return pos * param.voxel_size_;
}

__device__ int3 MapStruct::posVoxelToBlock(const int3& pos) const
				{
	int3 voxel = pos;

	if (voxel.x < 0)
		voxel.x -= BLOCK_SIZE_SUB_1;
	if (voxel.y < 0)
		voxel.y -= BLOCK_SIZE_SUB_1;
	if (voxel.z < 0)
		voxel.z -= BLOCK_SIZE_SUB_1;

	return voxel / BLOCK_SIZE;
}

__device__ int3 MapStruct::posBlockToVoxel(const int3& pos) const
				{
	return pos * BLOCK_SIZE;
}

__device__ int3 MapStruct::posVoxelToLocal(const int3& pos) const
				{
	int3 local = pos % BLOCK_SIZE;

	if (local.x < 0)
		local.x += BLOCK_SIZE;
	if (local.y < 0)
		local.y += BLOCK_SIZE;
	if (local.z < 0)
		local.z += BLOCK_SIZE;

	return local;
}

__device__ int MapStruct::posLocalToIdx(const int3& pos) const
				{
	return pos.z * BLOCK_SIZE * BLOCK_SIZE + pos.y * BLOCK_SIZE + pos.x;
}

__device__ int3 MapStruct::posIdxToLocal(const int& idx) const
				{
	uint x = idx % BLOCK_SIZE;
	uint y = idx % (BLOCK_SIZE * BLOCK_SIZE) / BLOCK_SIZE;
	uint z = idx / (BLOCK_SIZE * BLOCK_SIZE);
	return make_int3(x, y, z);
}

__device__ int3 MapStruct::posWorldToBlock(const float3& pos) const
				{
	return posVoxelToBlock(posWorldToVoxel(pos));
}

__device__ float3 MapStruct::posBlockToWorld(const int3& pos) const
				{
	return posVoxelToWorld(posBlockToVoxel(pos));
}

__device__ int MapStruct::posVoxelToIdx(const int3& pos) const
				{
	return posLocalToIdx(posVoxelToLocal(pos));
}

///////////////////////////////////////////////////////
// Implementation - Key Maps
///////////////////////////////////////////////////////
__device__ int KeyMap::Hash(const int3& pos)
{
	int res = ((pos.x * 73856093) ^ (pos.y * 19349669) ^ (pos.z * 83492791)) % KeyMap::MaxKeys;
	if (res < 0)
		res += KeyMap::MaxKeys;
	return res;
}

__device__ SURF * KeyMap::FindKey(const float3& pos)
{
	int3 blockPos = make_int3(pos / GridSize);
	int idx = Hash(blockPos);
	int bucketIdx = idx * nBuckets;
	for (int i = 0; i < nBuckets; ++i, ++bucketIdx)
	{
		SURF * key = &Keys[bucketIdx];
		if (key->valid)
		{
			if (make_int3(key->pos / GridSize) == blockPos)
				return key;
		}
	}
	return nullptr;
}

__device__ SURF * KeyMap::FindKey(const float3& pos, int& first, int& buck, int& hashIndex)
{
	first = -1;
	int3 p = make_int3(pos / GridSize);
	int idx = Hash(p);
	buck = idx;
	int bucketIdx = idx * nBuckets;
	for (int i = 0; i < nBuckets; ++i, ++bucketIdx)
	{
		SURF * key = &Keys[bucketIdx];
		if (!key->valid && first == -1)
			first = bucketIdx;

		if (key->valid)
		{
			int3 tmp = make_int3(key->pos / GridSize);
			if (tmp == p)
			{
				hashIndex = bucketIdx;
				return key;
			}
		}
	}

	return NULL;
}

__device__ void KeyMap::InsertKey(SURF* key, int& hashIndex)
{
	int buck = 0;
	int first = -1;
	SURF * oldKey = NULL;
//	if(hashIndex >= 0 && hashIndex < Keys.size) {
//		oldKey = &Keys[hashIndex];
//		if (oldKey && oldKey->valid) {
//			key->pos = oldKey->pos;
//			return;
//		}
//	}

	oldKey = FindKey(key->pos, first, buck, hashIndex);
	if (oldKey && oldKey->valid)
	{
		key->pos = oldKey->pos;
		return;
	}
	else if (first != -1)
	{
		int lock = atomicExch(&Mutex[buck], 1);
		if (lock < 0)
		{
			hashIndex = first;
			SURF * oldkey = &Keys[first];
			memcpy((void*) oldkey, (void*) key, sizeof(SURF));

			atomicExch(&Mutex[buck], -1);
			return;
		}
	}
}

__device__ void KeyMap::ResetKeys(int index)
{
	if (index < Mutex.size)
		Mutex[index] = -1;

	if (index < Keys.size)
	{
		Keys[index].valid = false;
	}
}
