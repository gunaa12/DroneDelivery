#pragma once

#include "tilemaps.hpp"

#include <fstream>
#include <stdio.h>
#include <ios>
#include <exception>
#include <stdint.h>

namespace maps2 {
	/*
	* Basic map to which tiles may be written and from which they may be read
	*/
	template <unsigned int log2_w, spacial_tile tile>
	class map_buffer : public map_iostream<tile>, protected stretchable_region<log2_w>, 
		public lim_tile_istream_vendor<tile>, lim_tile_istream_vendor<tile, gmtry2i::box_intersectable2i> {
		tree_info<log2_w> info;
		mixed_tree* root;
		tile_write_mode write_mode;

	protected:
		mixed_item<log2_w> get_top_item() {
			return mixed_item<log2_w>(root, info);
		}
		void stretch(const gmtry2i::vector2i& direction) {
			long map_init_width = 1 << (info.depth + log2_w);
			unsigned int old_root_index = (direction.x < 0) + 2 * (direction.y < 0);
			mixed_tree* new_root = new mixed_tree();
			new_root->branch[old_root_index] = root;
			root = new_root;
			info.depth += 1;
			info.origin -= gmtry2i::vector2i(direction.x < 0, direction.y < 0) * map_init_width;
		}
		inline void write_tile(const mixed_item<log2_w>& dst, const gmtry2i::vector2i& p, const tile* src) {
			tile* map_tile = static_cast<tile*>(alloc_mixed_item<log2_w, tile>(dst, p, 0).ptr);
			if (write_mode == TILE_ADD_MODE) *map_tile += *src;
			else *map_tile = *src;
		}

	public:
		map_buffer(gmtry2i::vector2i origin) {
			info = tree_info<log2_w>(origin, 1);
			root = new mixed_tree();
			write_mode = TILE_OVERWRITE_MODE;
		}
		const tile* read(const gmtry2i::vector2i& p) {
			if (!gmtry2i::contains(get_bounds(), p)) return 0;
			mixed_item<log2_w> deepest_item = seek_mixed_item(get_top_item(), p, 0);
			if (deepest_item.info.depth == 0) return static_cast<tile*>(deepest_item.ptr);
			else return 0;
		}
		tile_istream<tile>* read() {
			mixed_item<log2_w> top_item = get_top_item();
			return new tree_walker<log2_w, tile, gmtry2i::aligned_box2i>(top_item, top_item.info.get_bounds());
		}
		lim_tile_istream<tile, gmtry2i::aligned_box2i>* read(const gmtry2i::aligned_box2i& limit) {
			return new tree_walker<log2_w, tile, gmtry2i::aligned_box2i>(get_top_item(), limit);
		}
		lim_tile_istream<tile, gmtry2i::box_intersectable2i>* read(const gmtry2i::box_intersectable2i& limit) {
			return new tree_walker<log2_w, tile, gmtry2i::box_intersectable2i>(get_top_item(), limit);
		}
		void write(const gmtry2i::vector2i& p, const tile* src) {
			this->fit(p);
			write_tile(get_top_item(), p, src);
		}
		void write(tile_istream<tile>* src) {
			if (gmtry2i::area(src->get_bounds()) == 0) return;
			this->fit(src->get_bounds());
			tree_info<log2_w> virtual_min_dst_item = get_fitted_item_info<log2_w>(info, src->get_bounds());
			mixed_item<log2_w> min_dst = alloc_mixed_item<log2_w, tile>(get_top_item(), virtual_min_dst_item.origin, virtual_min_dst_item.depth);
			const tile* next_tile;
			tile* map_tile;
			while (next_tile = src->next()) {
				write_tile(min_dst, src->last_origin(), next_tile);
			}
		}
		gmtry2i::aligned_box2i get_bounds() const {
			return info.get_bounds();
		}
		tile_write_mode get_wmode() {
			return write_mode;
		}
		void set_wmode(tile_write_mode new_write_mode) {
			write_mode = new_write_mode;
		}
		~map_buffer() {
			delete_mixed_tree<tile>(root, info.depth);
			root = 0;
		}
	};

	/*
	* Basic map to which tiles may be written and from which neighboring tiles may be read
	* Existing tiles may be edited as neighbors through a returned tile
	* Tiles added as neighbors through a returned tile won't be recognized by this stream,
	*	deleted automatically, or linked with pre-existing or new tiles, and may be unintentionally overwritten.
	* WORK IN PROGRESS; NOT FULLY IMPLEMENTED
	*/
	template <unsigned int log2_w, spacial_tile tile>
	class map_nbrng_buffer : public map_ostream<tile>, public map_istream<nbrng_tile<tile>> {
		tree_info<log2_w> info;
		mixed_tree* root;
		tile_write_mode write_mode;

	protected:
		mixed_item<log2_w> get_top_item() {
			return mixed_item<log2_w>(root, info);
		}
		void stretch(const gmtry2i::vector2i& direction) {
			long map_init_width = 1 << (info.depth + log2_w);
			unsigned int old_root_index = (direction.x < 0) + 2 * (direction.y < 0);
			mixed_tree* new_root = new mixed_tree();
			new_root->branch[old_root_index] = root;
			root = new_root;
			info.depth += 1;
			info.origin -= gmtry2i::vector2i(direction.x < 0, direction.y < 0) * map_init_width;
		}
		void write_tile(const mixed_item<log2_w>& dst, const gmtry2i::vector2i& p, const nbrng_tile<tile>* src) {
			nbrng_tile<tile>* map_tile = static_cast<nbrng_tile<tile>*>(alloc_nbrng_tile<log2_w, tile>(dst, p).ptr);
			if (write_mode == TILE_ADD_MODE) map_tile->tile += src->tile;
			else map_tile->tile = src->tile;
		}

	public:
		map_nbrng_buffer(const gmtry2i::vector2i& origin) {

		}
	};

	/*
	* Stream for efficiently reading and writing tiles to a map file
	* File is closed when destructor is called
	* Tiles are not buffered
	*/
	template <unsigned int log2_w, spacial_tile tile>
	class map_fstream : public map_iostream<tile>, protected stretchable_region<log2_w>,
		public lim_tile_istream_vendor<tile>, public lim_tile_istream_vendor<tile, gmtry2i::box_intersectable2i> {
	protected:
		typedef std::uint32_t file_pos;
		const unsigned int tree_size = 4 * sizeof(file_pos);
		/*
		* Stores basic header information for file (IS NOT WRITTEN DIRECTLY TO FILE)
		* root: position of root tree in file
		* size: size of file
		*/
		struct bmap_file_header {
			unsigned int log2_tile_w;
			file_pos root;
			file_pos size;
		};
		/*
		* An index of something from the file
		*/
		struct file_pos_index {
			file_pos pos;
		};
		/*
		* An index used to remember spacial items within the tree that have already been read from the file
		*		If an item has been indexed, only one piece of contiguous memory will have to be read from the file in 
		*			order to access it.
		* Both trees and tiles are indexed as an index_tree
		*		A tile's index_tree will not have any nonzero branches
		* All subitems (even those that don't exist) of an item are indexed when any one of them are indexed,
			saving time in future searches.
		* Either none or all of an index tree's branches are indexed
		* pos (inherited from file_pos_index): position of the represented item in the file
		*/
		struct index_tree : public homogeneous_tree<file_pos_index> {
			index_tree(file_pos item_pos) : homogeneous_tree<file_pos_index>() {
				file_pos_index::pos = item_pos;
			}
		};
		typedef spacial_item<log2_w, homogeneous_tree<file_pos_index>> item_index;

	private:
		bmap_file_header map_header;
		tree_info<log2_w> info;
		std::uint32_t file_raw_depth;
		std::uint64_t file_raw_origin[2];
		std::uint32_t file_raw_log2_tile_w;
		const unsigned int file_raw_header_size = sizeof(file_raw_depth) + 2 * sizeof(*file_raw_origin) +
			sizeof(file_raw_log2_tile_w) + 2 * sizeof(file_pos);
		std::fstream file;
		std::string file_name;
		index_tree* indices;
		tile_write_mode writemode;
		bool header_has_unsaved_changes;
		const tile blank = tile();
		tile last_tile;

	protected:
		inline void read_file(void* dst, file_pos pos, unsigned long len) {
			file.seekg(pos);
			file.read(static_cast<char*>(dst), len);
		}
		// cannot be used to append
		inline void write_file(const void* src, file_pos pos, unsigned long len) {
			file.seekp(pos);
			file.write(static_cast<const char*>(src), len);
		}
		inline void append_file(const void* src, unsigned long len) {
			file.seekp(map_header.size);
			file.write(static_cast<const char*>(src), len);
			map_header.size += len;
			header_has_unsaved_changes = true;
			DEBUG_PRINT("Added to file size: " << len); //test
			DEBUG_PRINT("New file size: " << map_header.size); //test
		}
		inline void write_branch(file_pos new_branch, unsigned int branch_index, file_pos tree_pos) {
			DEBUG_PRINT(new_branch << " written to branch " << branch_index << " of tree at " << tree_pos); //test
			file.seekp(tree_pos + sizeof(new_branch) * branch_index);
			file.write(reinterpret_cast<char*>(&new_branch), sizeof(new_branch));

			file_pos next_branches[4]; //test
			read_file(next_branches, tree_pos, tree_size); //test
			DEBUG_PRINT("Resultant tree at " << tree_pos << ":"); //test
			DEBUG_PRINT("{ " << next_branches[0] << ", " << next_branches[1] << ", " <<
				next_branches[2] << ", " << next_branches[3] << " }"); //test
		}
		inline void write_tile(const tile* src, file_pos pos) {
			DEBUG_PRINT("Tile written to file at " << pos); //test
			if (writemode == TILE_ADD_MODE) {
				tile current_tile = tile();
				read_file(&current_tile, pos, sizeof(tile));
				current_tile += *src;
				write_file(&current_tile, pos, sizeof(tile));
			}
			else {
				write_file(src, pos, sizeof(tile));
			}
		}
		void read_header() {
			file_pos current_pos = 0;
			unsigned int member_size;

			read_file(&file_raw_depth, current_pos, member_size = sizeof(file_raw_depth));
			current_pos += member_size;
			info.depth = static_cast<unsigned int>(file_raw_depth);

			read_file(file_raw_origin, current_pos, member_size = 2 * sizeof(*file_raw_origin));
			current_pos += member_size;
			info.origin.x = static_cast<unsigned long>(file_raw_origin[0]);
			info.origin.y = static_cast<unsigned long>(file_raw_origin[1]);

			read_file(&file_raw_log2_tile_w, current_pos, member_size = sizeof(file_raw_log2_tile_w));
			current_pos += member_size;
			map_header.log2_tile_w = static_cast<unsigned int>(file_raw_log2_tile_w);

			read_file(&map_header.root, current_pos, 2 * sizeof(file_pos));
		}
		void write_header() {
			file_pos current_pos = 0;
			unsigned int member_size;

			file_raw_depth = static_cast<std::uint32_t>(info.depth);
			write_file(&file_raw_depth, current_pos, member_size = sizeof(file_raw_depth));
			current_pos += member_size;

			file_raw_origin[0] = static_cast<std::uint64_t>(info.origin.x);
			file_raw_origin[1] = static_cast<std::uint64_t>(info.origin.y);
			write_file(file_raw_origin, current_pos, member_size = 2 * sizeof(*file_raw_origin));
			current_pos += member_size;

			file_raw_log2_tile_w = static_cast<std::uint32_t>(map_header.log2_tile_w);
			write_file(&file_raw_log2_tile_w, current_pos, member_size = sizeof(file_raw_log2_tile_w));
			current_pos += member_size;

			write_file(&map_header.root, current_pos, 2 * sizeof(file_pos));

			DEBUG_PRINT("Header saved!"); //test
			header_has_unsaved_changes = false;
		}
		inline gmtry2i::aligned_box2i get_map_bounds() const {
			return info.get_bounds();
		}
		void stretch(const gmtry2i::vector2i& direction) {
			long map_init_width = 1 << (info.depth + log2_w);
			unsigned int old_root_index = (direction.x < 0) + 2 * (direction.y < 0);
			index_tree* new_root = new index_tree(map_header.size);
			new_root->branch[old_root_index] = indices;
			indices = new_root;
			for (int i = 0; i < 4; i++) if (i != old_root_index) indices->branch[i] = new index_tree(0);

			file_pos new_root_branches[4] = { 0 };
			new_root_branches[old_root_index] = map_header.root;
			append_file(new_root_branches, tree_size);

			map_header.root = new_root->pos;
			info.depth += 1;
			info.origin -= gmtry2i::vector2i(direction.x < 0, direction.y < 0) * map_init_width;
			header_has_unsaved_changes = true;
			DEBUG_PRINT("Map Expanded!"); //test
			DEBUG_PRINT("New root: " << map_header.root); //test
			DEBUG_PRINT("New depth: " << info.depth); //test
			DEBUG_PRINT("New origin: " << info.origin.x << ", " << info.origin.y); //test
		}
		item_index get_top_item() {
			return item_index(indices, info);
		}
		/*
		* Returns the existing item closest to the desired depth that contains the given point and is descended from start
		* Start must exist, and the returned item won't contain the point if start doesn't contain the point
		*/
		item_index seek_item(const item_index& start, const gmtry2i::vector2i& p, unsigned int depth) {
			item_index item = item_index();
			item_index next_item = start;
			unsigned int next_branch_idx;
			file_pos next_branches[4];
			while (next_item.ptr->pos && next_item.info.depth > depth) {
				item = next_item;
				next_branch_idx = item.info.get_branch_idx(p);
				if (item.ptr->branch[next_branch_idx] == 0) {
					read_file(next_branches, item.ptr->pos, tree_size);
					for (int i = 0; i < 4; i++) item.ptr->branch[i] = new index_tree(next_branches[i]);

					DEBUG_PRINT("Tree at depth " << item.info.depth << " indexed from " << item.ptr->pos << ":"); //test
					DEBUG_PRINT("{ " << next_branches[0] << ", " << next_branches[1] << ", " <<
						next_branches[2] << ", " << next_branches[3] << " }"); //test
				}
				next_item = item.get_branch_item(next_branch_idx);
			}
			return next_item.ptr->pos ? next_item : item;
		}
		/*
		* Allocates an item at the desired position and depth and returns its index
		* Returns a real item if start is real, although it won't contain the point if start doesn't contain the point
		*/
		item_index alloc_item(const item_index& start, const gmtry2i::vector2i p, unsigned int depth) {
			item_index item = seek_item(start, p, depth);
			if (item.info.depth == depth) return item;
			unsigned int next_branch_idx = item.info.get_branch_idx(p);
			write_branch(map_header.size, next_branch_idx, item.ptr->pos);
			// the only items that make it to this point have a next_item with a null pos
			item.ptr->branch[next_branch_idx]->pos = map_header.size;
			item = item.get_branch_item(next_branch_idx);
			while (item.info.depth > depth) {
				file_pos branches[4] = { 0, 0, 0, 0 };
				next_branch_idx = item.info.get_branch_idx(p);
				branches[next_branch_idx] = map_header.size + tree_size;
				append_file(branches, tree_size);
				for (int i = 0; i < 4; i++) item.ptr->branch[i] = new index_tree(branches[i]);

				DEBUG_PRINT("File appended with tree at " << (branches[next_branch_idx] - tree_size) <<
					" at depth " << item.info.depth << ":"); //test
				DEBUG_PRINT("{ " << branches[0] << ", " << branches[1] << ", " <<
					branches[2] << ", " << branches[3] << " }"); //test

				item = item.get_branch_item(next_branch_idx);
			}
			append_file(&blank, depth ? tree_size : sizeof(tile));
			return item;
		}

		template <gmtry2i::intersectable2i<> limiter_type>
		class map_fstream_tstream : public tree_walker<log2_w, tile, limiter_type> {
			map_fstream* src;
			tile last_tile;
		protected:
			inline void* get_next_item(void* current_item_ptr, unsigned int branch_index) {
				index_tree* current_item = static_cast<index_tree*>(current_item_ptr);
				// if branches of current_item haven't been loaded yet, load them in
				if (current_item->branch[0] == 0) {
					file_pos branches[4] = { 0, 0, 0, 0 }; //remove 0 initializations
					src->read_file(branches, current_item->pos, src->tree_size);
					for (int i = 0; i < 4; i++) current_item->branch[i] = new index_tree(branches[i]);

					DEBUG_PRINT("Tree indexed from " << current_item->pos << ":" << 
								"{ " << branches[0] << ", " << branches[1] << ", " <<
										branches[2] << ", " << branches[3] << " }"); //test
				}
				index_tree* next_item = static_cast<index_tree*>(current_item->branch[branch_index]);
				if (next_item->pos == 0) next_item = 0;
				return next_item;
			}
			inline tile* get_tile(void* item) {
				src->read_file(&last_tile, static_cast<index_tree*>(item)->pos, sizeof(last_tile));
				DEBUG_PRINT("Tile built from " << static_cast<index_tree*>(item)->pos);
				return &last_tile;
			}
		public:
			map_fstream_tstream(const item_index& item, const limiter_type& limiter, map_fstream* source) :
				tree_walker<log2_w, tile, limiter_type>(mixed_item<log2_w>(item.ptr, item.info), limiter) {
				src = source;
				last_tile = tile();
			}
		};

	public:
		map_fstream(const std::string& fname, const gmtry2i::vector2i& origin) {
			file_name = fname;
			std::ios::openmode filemode = std::ios::binary | std::ios::in | std::ios::out;
			file.open(file_name, filemode);
			if (!(file.is_open())) {
				FILE* tmp_file = 0;
				fopen_s(&tmp_file, file_name.c_str(), "w");
				fclose(tmp_file);
				file.open(file_name, filemode);
				info = tree_info<log2_w>(origin, 1);
				map_header.log2_tile_w = log2_w;
				map_header.root = file_raw_header_size;
				map_header.size = file_raw_header_size;
				write_header();
				file_pos root_branches[4] = { 0, 0, 0, 0 };
				append_file(root_branches, tree_size);
				DEBUG_PRINT("File created!"); //test
			}
			else {
				read_header();
				DEBUG_PRINT("File already exists!"); //test
			}
			if (!(file.is_open())) throw std::ios::failure("Map file cannot be opened");

			DEBUG_PRINT("Recorded log2_tile_w: " << map_header.log2_tile_w); //test
			DEBUG_PRINT("Recorded depth: " << info.depth); //test
			DEBUG_PRINT("Recorded origin: " << info.origin.x << ", " << info.origin.y); //test
			DEBUG_PRINT("Recorded root: " << map_header.root); //test
			DEBUG_PRINT("Recorded file length: " << map_header.size); //test


			if (log2_w != map_header.log2_tile_w || map_header.size < file_raw_header_size)
				throw std::ios::failure("Map file formatted improperly");

			last_tile = blank;
			indices = new index_tree(map_header.root);
			writemode = TILE_OVERWRITE_MODE;
			header_has_unsaved_changes = false;
		}
		const tile* read(const gmtry2i::vector2i& p) {
			if (!(file.is_open())) throw std::ios::failure("Cannot read map file");
			if (!gmtry2i::contains(get_map_bounds(), p)) return 0;
			item_index deepest_item = seek_item(get_top_item(), p, 0);
			if (deepest_item.info.depth == 0) {
				read_file(&last_tile, deepest_item.ptr->pos, sizeof(tile));
				return &last_tile;
			}
			else return 0;
		}
		tile_istream<tile>* read() {
			if (!(file.is_open())) throw std::ios::failure("Cannot read map file");
			item_index top_item = get_top_item();
			return new map_fstream_tstream<gmtry2i::aligned_box2i>(top_item, top_item.info.get_bounds(), this);
		}
		lim_tile_istream<tile, gmtry2i::aligned_box2i>* read(const gmtry2i::aligned_box2i& limit) {
			if (!(file.is_open())) throw std::ios::failure("Cannot read map file");
			return new map_fstream_tstream<gmtry2i::aligned_box2i>(get_top_item(), limit, this);
		}
		lim_tile_istream<tile, gmtry2i::box_intersectable2i>* read(const gmtry2i::box_intersectable2i& limit) {
			if (!(file.is_open())) throw std::ios::failure("Cannot read map file");
			return new map_fstream_tstream<gmtry2i::box_intersectable2i>(get_top_item(), limit, this);
		}
		void write(const gmtry2i::vector2i& p, const tile* src) {
			if (!(file.is_open())) throw std::ios::failure("Cannot write to map file");
			this->fit(p);
			item_index dst = alloc_item(get_top_item(), p, 0);
			write_tile(src, dst.ptr->pos);
			DEBUG_PRINT("File appended with new tile!"); //test
		}
		void write(tile_istream<tile>* src) {
			if (!(file.is_open())) throw std::ios::failure("Cannot write to map file");
			if (gmtry2i::area(src->get_bounds()) == 0) return;
			this->fit(src->get_bounds());
			DEBUG_PRINT("Bounds of incoming tile stream: " << gmtry2i::to_string(src->get_bounds())); //test
			tree_info<log2_w> virtual_min_dst = get_fitted_item_info<log2_w>(info, src->get_bounds());
			DEBUG_PRINT("Bounds of virtual destination for incoming tiles: " <<
				gmtry2i::to_string(virtual_min_dst.get_bounds())); //test
			item_index min_dst = alloc_item(get_top_item(), virtual_min_dst.origin, virtual_min_dst.depth);
			const tile* stream_tile;
			item_index map_tile;
			while (stream_tile = src->next()) {
				map_tile = alloc_item(min_dst, src->last_origin(), 0);
				//std::cout << "Depth of written tile (better be 0 or else ima go insaneo style): " << map_tile.depth << std::endl; //test
				DEBUG_PRINT("Origin of written tile: " << gmtry2i::to_string(map_tile.info.origin)); //test
				write_tile(stream_tile, map_tile.ptr->pos);
			}
		}
		gmtry2i::aligned_box2i get_bounds() const {
			return get_map_bounds();
		}
		tile_write_mode get_wmode() {
			return writemode;
		}
		void set_wmode(tile_write_mode new_write_mode) {
			writemode = new_write_mode;
		}
		void flush() {
			if (header_has_unsaved_changes) write_header();
		}
		~map_fstream() {
			if (file.is_open()) {
				DEBUG_PRINT("Final size of map file: " << map_header.size); //test
				flush();
				file.close();
			}
			delete_homogeneous_tree(indices, info.depth);
		}
	};
}