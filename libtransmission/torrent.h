/*
 * This file Copyright (C) 2009-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "bandwidth.h"
#include "bitfield.h"
#include "block-info.h"
#include "completion.h"
#include "file.h"
#include "file-piece-map.h"
#include "interned-string.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-macros.h"

class tr_swarm;
struct tr_error;
struct tr_magnet_info;
struct tr_metainfo_parsed;
struct tr_session;
struct tr_torrent;
struct tr_announcer_tiers;

/**
***  Package-visible ctor API
**/

void tr_torrentFree(tr_torrent* tor);

void tr_ctorSetSave(tr_ctor* ctor, bool saveMetadataInOurTorrentsDir);

bool tr_ctorGetSave(tr_ctor const* ctor);

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor);

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor);

bool tr_ctorSaveContents(tr_ctor const* ctor, std::string_view filename, tr_error** error);

std::string_view tr_ctorGetContents(tr_ctor const* ctor);

tr_session* tr_ctorGetSession(tr_ctor const* ctor);

bool tr_ctorGetIncompleteDir(tr_ctor const* ctor, char const** setmeIncompleteDir);

/**
***
**/

using tr_labels_t = std::unordered_set<std::string>;

void tr_torrentSetLabels(tr_torrent* tor, tr_labels_t&& labels);

void tr_torrentChangeMyPort(tr_torrent* session);

tr_torrent* tr_torrentFindFromObfuscatedHash(tr_session* session, tr_sha1_digest_t const& hash);

bool tr_torrentReqIsValid(tr_torrent const* tor, tr_piece_index_t index, uint32_t offset, uint32_t length);

void tr_torrentGetBlockLocation(
    tr_torrent const* tor,
    tr_block_index_t block,
    tr_piece_index_t* piece,
    uint32_t* offset,
    uint32_t* length);

tr_block_span_t tr_torGetFileBlockSpan(tr_torrent const* tor, tr_file_index_t file);

void tr_torrentCheckSeedLimit(tr_torrent* tor);

/** save a torrent's .resume file if it's changed since the last time it was saved */
void tr_torrentSave(tr_torrent* tor);

enum tr_verify_state
{
    TR_VERIFY_NONE,
    TR_VERIFY_WAIT,
    TR_VERIFY_NOW
};

tr_torrent_activity tr_torrentGetActivity(tr_torrent const* tor);

struct tr_incomplete_metadata;

/** @brief Torrent object */
struct tr_torrent : public tr_completion::torrent_view
{
public:
    explicit tr_torrent(tr_info const& inf)
        : block_info{ inf.totalSize(), inf.pieceSize() }
        , completion{ this, &this->block_info }
    {
    }

    ~tr_torrent() override = default;

    void setLocation(
        std::string_view location,
        bool move_from_current_location,
        double volatile* setme_progress,
        int volatile* setme_state);

    void renamePath(
        std::string_view oldpath,
        std::string_view newname,
        tr_torrent_rename_done_func callback,
        void* callback_user_data);

    tr_sha1_digest_t pieceHash(tr_piece_index_t i) const
    {
        TR_ASSERT(i < std::size(this->piece_checksums_));
        return this->piece_checksums_[i];
    }

    // these functions should become private when possible,
    // but more refactoring is needed before that can happen
    // because much of tr_torrent's impl is in the non-member C bindings
    //
    // private:
    void swapMetainfo(tr_metainfo_parsed& parsed);

    auto unique_lock() const
    {
        return session->unique_lock();
    }

    /// SPEED LIMIT

    void setSpeedLimitBps(tr_direction, unsigned int Bps);

    unsigned int speedLimitBps(tr_direction) const;

    /// BLOCK INFO

    [[nodiscard]] constexpr auto const& blockInfo() const
    {
        return block_info;
    }

    [[nodiscard]] constexpr auto blockCount() const
    {
        return blockInfo().blockCount();
    }
    [[nodiscard]] constexpr auto blockOf(uint64_t offset) const
    {
        return blockInfo().blockOf(offset);
    }
    [[nodiscard]] constexpr auto blockOf(tr_piece_index_t piece, uint32_t offset, uint32_t length = 0) const
    {
        return blockInfo().blockOf(piece, offset, length);
    }
    [[nodiscard]] constexpr auto blockSize() const
    {
        return blockInfo().blockSize();
    }
    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const
    {
        return blockInfo().blockSize(block);
    }
    [[nodiscard]] constexpr auto blockSpanForPiece(tr_piece_index_t piece) const
    {
        return blockInfo().blockSpanForPiece(piece);
    }
    [[nodiscard]] constexpr auto offset(tr_piece_index_t piece, uint32_t offset, uint32_t length = 0) const
    {
        return blockInfo().offset(piece, offset, length);
    }
    [[nodiscard]] constexpr auto pieceCount() const
    {
        return blockInfo().pieceCount();
    }
    [[nodiscard]] constexpr auto pieceForBlock(tr_block_index_t block) const
    {
        return blockInfo().pieceForBlock(block);
    }
    [[nodiscard]] constexpr auto pieceOf(uint64_t offset) const
    {
        return blockInfo().pieceOf(offset);
    }
    [[nodiscard]] constexpr auto pieceSize() const
    {
        return blockInfo().pieceSize();
    }
    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const
    {
        return blockInfo().pieceSize(piece);
    }
    [[nodiscard]] constexpr auto totalSize() const
    {
        return blockInfo().totalSize();
    }

    /// COMPLETION

    [[nodiscard]] uint64_t leftUntilDone() const
    {
        return completion.leftUntilDone();
    }

    [[nodiscard]] bool hasAll() const
    {
        return completion.hasAll();
    }

    [[nodiscard]] bool hasNone() const
    {
        return completion.hasNone();
    }

    [[nodiscard]] bool hasPiece(tr_piece_index_t piece) const
    {
        return completion.hasPiece(piece);
    }

    [[nodiscard]] bool hasBlock(tr_block_index_t block) const
    {
        return completion.hasBlock(block);
    }

    [[nodiscard]] size_t countMissingBlocksInPiece(tr_piece_index_t piece) const
    {
        return completion.countMissingBlocksInPiece(piece);
    }

    [[nodiscard]] size_t countMissingBytesInPiece(tr_piece_index_t piece) const
    {
        return completion.countMissingBytesInPiece(piece);
    }

    [[nodiscard]] uint64_t hasTotal() const
    {
        return completion.hasTotal();
    }

    [[nodiscard]] std::vector<uint8_t> createPieceBitfield() const
    {
        return completion.createPieceBitfield();
    }

    [[nodiscard]] constexpr bool isDone() const
    {
        return completeness != TR_LEECH;
    }

    [[nodiscard]] constexpr bool isSeed() const
    {
        return completeness == TR_SEED;
    }

    [[nodiscard]] constexpr bool isPartialSeed() const
    {
        return completeness == TR_PARTIAL_SEED;
    }

    [[nodiscard]] tr_bitfield const& blocks() const
    {
        return completion.blocks();
    }

    void amountDoneBins(float* tab, int n_tabs) const
    {
        return completion.amountDone(tab, n_tabs);
    }

    void setBlocks(tr_bitfield blocks);

    void setHasPiece(tr_piece_index_t piece, bool has)
    {
        completion.setHasPiece(piece, has);
    }

    /// FILE <-> PIECE

    [[nodiscard]] auto piecesInFile(tr_file_index_t file) const
    {
        return fpm_.pieceSpan(file);
    }

    [[nodiscard]] auto fileOffset(uint64_t offset) const
    {
        return fpm_.fileOffset(offset);
    }

    [[nodiscard]] auto fileOffset(tr_piece_index_t piece, uint32_t piece_offset) const
    {
        return fpm_.fileOffset(this->offset(piece, piece_offset));
    }

    /// WANTED

    [[nodiscard]] bool pieceIsWanted(tr_piece_index_t piece) const final
    {
        return files_wanted_.pieceWanted(piece);
    }

    [[nodiscard]] bool fileIsWanted(tr_file_index_t file) const
    {
        return files_wanted_.fileWanted(file);
    }

    void initFilesWanted(tr_file_index_t const* files, size_t n_files, bool wanted)
    {
        setFilesWanted(files, n_files, wanted, /*is_bootstrapping*/ true);
    }

    void setFilesWanted(tr_file_index_t const* files, size_t n_files, bool wanted)
    {
        setFilesWanted(files, n_files, wanted, /*is_bootstrapping*/ false);
    }

    void recheckCompleteness(); // TODO(ckerr): should be private

    /// PRIORITIES

    [[nodiscard]] tr_priority_t piecePriority(tr_piece_index_t piece) const
    {
        return file_priorities_.piecePriority(piece);
    }

    void setFilePriorities(tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority)
    {
        file_priorities_.set(files, fileCount, priority);
        setDirty();
    }

    void setFilePriority(tr_file_index_t file, tr_priority_t priority)
    {
        file_priorities_.set(file, priority);
        setDirty();
    }

    /// LOCATION

    [[nodiscard]] tr_interned_string currentDir() const
    {
        return this->current_dir;
    }

    [[nodiscard]] tr_interned_string downloadDir() const
    {
        return this->download_dir;
    }

    [[nodiscard]] tr_interned_string incompleteDir() const
    {
        return this->incomplete_dir;
    }

    /// METAINFO - FILES

    [[nodiscard]] tr_file_index_t fileCount() const
    {
        return std::size(info.files);
    }

    [[nodiscard]] std::string const& fileSubpath(tr_file_index_t i) const
    {
        return info.fileSubpath(i);
    }

    [[nodiscard]] auto fileSize(tr_file_index_t i) const
    {
        return info.fileSize(i);
    }

    void setFileSubpath(tr_file_index_t i, std::string_view subpath);

    struct tr_found_file_t : public tr_sys_path_info
    {
        std::string& filename; // /home/foo/Downloads/torrent/01-file-one.txt
        std::string_view base; // /home/foo/Downloads
        std::string_view subpath; // /torrent/01-file-one.txt

        tr_found_file_t(tr_sys_path_info info, std::string& f, std::string_view b)
            : tr_sys_path_info{ info }
            , filename{ f }
            , base{ b }
            , subpath{ f.c_str() + std::size(b) + 1 }
        {
        }
    };

    std::optional<tr_found_file_t> findFile(std::string& filename, tr_file_index_t i) const;

    /// METAINFO - TRACKERS

    [[nodiscard]] auto const& announceList() const
    {
        return *this->info.announce_list;
    }

    [[nodiscard]] auto& announceList()
    {
        return *this->info.announce_list;
    }

    [[nodiscard]] auto trackerCount() const
    {
        return std::size(this->announceList());
    }

    [[nodiscard]] auto const& tracker(size_t i) const
    {
        return this->announceList().at(i);
    }

    [[nodiscard]] auto tiers() const
    {
        return this->announceList().tiers();
    }

    /// METAINFO - WEBSEEDS

    [[nodiscard]] auto webseedCount() const
    {
        return info.webseedCount();
    }

    [[nodiscard]] auto const& webseed(size_t i) const
    {
        return info.webseed(i);
    }

    /// METAINFO - OTHER

    void setName(std::string_view name);

    [[nodiscard]] auto const& name() const
    {
        return this->info.name();
    }

    [[nodiscard]] auto const& infoHash() const
    {
        return this->info.infoHash();
    }

    [[nodiscard]] auto isPrivate() const
    {
        return this->info.isPrivate();
    }

    [[nodiscard]] auto isPublic() const
    {
        return !this->isPrivate();
    }

    [[nodiscard]] auto const& infoHashString() const
    {
        return this->info.infoHashString();
    }

    [[nodiscard]] auto dateCreated() const
    {
        return this->info.dateCreated();
    }

    [[nodiscard]] auto const& torrentFile() const
    {
        return this->info.torrentFile();
    }

    [[nodiscard]] auto const& comment() const
    {
        return this->info.comment();
    }

    [[nodiscard]] auto const& creator() const
    {
        return this->info.creator();
    }

    [[nodiscard]] auto const& source() const
    {
        return this->info.source();
    }

    [[nodiscard]] auto hasMetadata() const
    {
        return fileCount() > 0;
    }

    [[nodiscard]] auto infoDictSize() const
    {
        return this->info_dict_size;
    }

    [[nodiscard]] auto infoDictOffset() const
    {
        return this->info_dict_offset;
    }

    /// METAINFO - CHECKSUMS

    [[nodiscard]] bool ensurePieceIsChecked(tr_piece_index_t piece)
    {
        TR_ASSERT(piece < this->pieceCount());

        if (checked_pieces_.test(piece))
        {
            return true;
        }

        bool const checked = checkPiece(piece);
        this->markChanged();
        this->setDirty();

        checked_pieces_.set(piece, checked);
        return checked;
    }

    void initCheckedPieces(tr_bitfield const& checked, time_t const* mtimes /*fileCount()*/)
    {
        TR_ASSERT(std::size(checked) == this->pieceCount());
        checked_pieces_ = checked;

        auto const n = this->fileCount();
        this->file_mtimes_.resize(n);

        auto filename = std::string{};
        for (size_t i = 0; i < n; ++i)
        {
            auto const found = this->findFile(filename, i);
            auto const mtime = found ? found->last_modified_at : 0;

            this->file_mtimes_[i] = mtime;

            // if a file has changed, mark its pieces as unchecked
            if (mtime == 0 || mtime != mtimes[i])
            {
                auto const [begin, end] = piecesInFile(i);
                checked_pieces_.unsetSpan(begin, end);
            }
        }
    }

    ///

    [[nodiscard]] auto isQueued() const
    {
        return this->is_queued;
    }

    [[nodiscard]] constexpr auto queueDirection() const
    {
        return this->isDone() ? TR_UP : TR_DOWN;
    }

    [[nodiscard]] auto allowsPex() const
    {
        return this->isPublic() && this->session->isPexEnabled;
    }

    [[nodiscard]] auto allowsDht() const
    {
        return this->isPublic() && tr_sessionAllowsDHT(this->session);
    }

    [[nodiscard]] auto allowsLpd() const // local peer discovery
    {
        return this->isPublic() && tr_sessionAllowsLPD(this->session);
    }

    [[nodiscard]] bool isPieceTransferAllowed(tr_direction direction) const;

    [[nodiscard]] bool clientCanDownload() const
    {
        return this->isPieceTransferAllowed(TR_PEER_TO_CLIENT);
    }

    [[nodiscard]] bool clientCanUpload() const
    {
        return this->isPieceTransferAllowed(TR_CLIENT_TO_PEER);
    }

    void setLocalError(std::string_view errmsg)
    {
        this->error = TR_STAT_LOCAL_ERROR;
        this->error_announce_url = TR_KEY_NONE;
        this->error_string = errmsg;
    }

    void setVerifyState(tr_verify_state state);

    void setDateActive(time_t t);

    /** Return the mime-type (e.g. "audio/x-flac") that matches more of the
        torrent's content than any other mime-type. */
    std::string_view primaryMimeType() const;

    tr_info info = {};

    tr_bitfield checked_pieces_ = tr_bitfield{ 0 };

    tr_block_info block_info;

    // TODO(ckerr): make private once some of torrent.cc's `tr_torrentFoo()` methods are member functions
    tr_completion completion;

    tr_session* session = nullptr;

    tr_announcer_tiers* announcer_tiers = nullptr;

    // Changed to non-owning pointer temporarily till tr_torrent becomes C++-constructible and destructible
    // TODO: change tr_bandwidth* to owning pointer to the bandwidth, or remove * and own the value
    Bandwidth* bandwidth = nullptr;

    tr_swarm* swarm = nullptr;

    // TODO: is this actually still needed?
    int const magicNumber = MagicNumber;

    std::optional<double> verify_progress;

    tr_stat_errtype error = TR_STAT_OK;
    tr_interned_string error_announce_url;
    std::string error_string;

    bool checkPiece(tr_piece_index_t piece);

    tr_sha1_digest_t obfuscated_hash = {};

    /* Used when the torrent has been created with a magnet link
     * and we're in the process of downloading the metainfo from
     * other peers */
    struct tr_incomplete_metadata* incompleteMetadata = nullptr;

    /* If the initiator of the connection receives a handshake in which the
     * peer_id does not match the expected peerid, then the initiator is
     * expected to drop the connection. Note that the initiator presumably
     * received the peer information from the tracker, which includes the
     * peer_id that was registered by the peer. The peer_id from the tracker
     * and in the handshake are expected to match.
     */
    std::optional<tr_peer_id_t> peer_id;

    time_t peer_id_creation_time = 0;

    // Where the files are when the torrent is complete.
    tr_interned_string download_dir;

    // Where the files are when the torrent is incomplete.
    // a value of TR_KEY_NONE indicates the 'incomplete_dir' feature is unused
    tr_interned_string incomplete_dir;

    // Where the files are now.
    // Will equal either download_dir or incomplete_dir
    tr_interned_string current_dir;

    /* Length, in bytes, of the "info" dict in the .torrent file. */
    uint64_t info_dict_size = 0;

    /* Offset, in bytes, of the beginning of the "info" dict in the .torrent file.
     *
     * Used by the torrent-magnet code for serving metainfo to peers.
     * This field is lazy-generated and might not be initialized yet. */
    uint64_t info_dict_offset = 0;

    bool info_dict_offset_is_cached = false;

    tr_completeness completeness = TR_LEECH;

    time_t dhtAnnounceAt = 0;
    time_t dhtAnnounce6At = 0;
    bool dhtAnnounceInProgress = false;
    bool dhtAnnounce6InProgress = false;

    time_t lpdAnnounceAt = 0;

    uint64_t downloadedCur = 0;
    uint64_t downloadedPrev = 0;
    uint64_t uploadedCur = 0;
    uint64_t uploadedPrev = 0;
    uint64_t corruptCur = 0;
    uint64_t corruptPrev = 0;

    uint64_t etaDLSpeedCalculatedAt = 0;
    uint64_t etaULSpeedCalculatedAt = 0;
    unsigned int etaDLSpeed_Bps = 0;
    unsigned int etaULSpeed_Bps = 0;

    time_t activityDate = 0;
    time_t addedDate = 0;
    time_t anyDate = 0;
    time_t doneDate = 0;
    time_t editDate = 0;
    time_t startDate = 0;

    int secondsDownloading = 0;
    int secondsSeeding = 0;

    int queuePosition = 0;

    tr_torrent_metadata_func metadata_func = nullptr;
    void* metadata_func_user_data = nullptr;

    tr_torrent_completeness_func completeness_func = nullptr;
    void* completeness_func_user_data = nullptr;

    tr_torrent_ratio_limit_hit_func ratio_limit_hit_func = nullptr;
    void* ratio_limit_hit_func_user_data = nullptr;

    tr_torrent_idle_limit_hit_func idle_limit_hit_func = nullptr;
    void* idle_limit_hit_func_user_data = nullptr;

    void* queue_started_user_data = nullptr;
    void (*queue_started_callback)(tr_torrent*, void* queue_started_user_data) = nullptr;

    bool isDeleting = false;
    bool isDirty = false;
    bool is_queued = false;
    bool isRunning = false;
    bool isStopping = false;
    bool startAfterVerify = false;

    bool prefetchMagnetMetadata = false;
    bool magnetVerify = false;

    void setDirty()
    {
        this->isDirty = true;
    }

    void markEdited();
    void markChanged();

    uint16_t maxConnectedPeers = TR_DEFAULT_PEER_LIMIT_TORRENT;

    tr_verify_state verifyState = TR_VERIFY_NONE;

    time_t lastStatTime = 0;
    tr_stat stats = {};

    int uniqueId = 0;

    float desiredRatio = 0.0F;
    tr_ratiolimit ratioLimitMode = TR_RATIOLIMIT_GLOBAL;

    uint16_t idleLimitMinutes = 0;
    tr_idlelimit idleLimitMode = TR_IDLELIMIT_GLOBAL;
    bool finishedSeedingByIdle = false;

    tr_labels_t labels;

    static auto constexpr MagicNumber = int{ 95549 };

    tr_file_piece_map fpm_ = tr_file_piece_map{ info };
    tr_file_priorities file_priorities_{ &fpm_ };
    tr_files_wanted files_wanted_{ &fpm_ };

    std::vector<time_t> file_mtimes_;

private:
    void setFilesWanted(tr_file_index_t const* files, size_t n_files, bool wanted, bool is_bootstrapping)
    {
        auto const lock = unique_lock();

        files_wanted_.set(files, n_files, wanted);
        completion.invalidateSizeWhenDone();

        if (!is_bootstrapping)
        {
            setDirty();
            recheckCompleteness();
        }
    }

    mutable std::vector<tr_sha1_digest_t> piece_checksums_;
};

/***
****
***/

constexpr bool tr_isTorrent(tr_torrent const* tor)
{
    return tor != nullptr && tor->magicNumber == tr_torrent::MagicNumber && tr_isSession(tor->session);
}

/**
 * Tell the tr_torrent that it's gotten a block
 */
void tr_torrentGotBlock(tr_torrent* tor, tr_block_index_t blockIndex);

/**
 * @brief Like tr_torrentFindFile(), but splits the filename into base and subpath.
 *
 * If the file is found, "tr_strvPath(base, subpath, nullptr)"
 * will generate the complete filename.
 *
 * @return true if the file is found, false otherwise.
 *
 * @param base if the torrent is found, this will be either
 *             tor->downloadDir or tor->incompleteDir
 * @param subpath on success, this pointer is assigned a newly-allocated
 *                string holding the second half of the filename.
 */
bool tr_torrentFindFile2(tr_torrent const*, tr_file_index_t fileNo, char const** base, char** subpath, time_t* mtime);

/* Returns a newly-allocated version of the tr_file.name string
 * that's been modified to denote that it's not a complete file yet.
 * In the current implementation this is done by appending ".part"
 * a la Firefox. */
char* tr_torrentBuildPartial(tr_torrent const*, tr_file_index_t fileNo);

/* for when the info dict has been fundamentally changed wrt files,
 * piece size, etc. such as in BEP 9 where peers exchange metadata */
void tr_torrentGotNewInfoDict(tr_torrent* tor);

tr_peer_id_t const& tr_torrentGetPeerId(tr_torrent* tor);

/** @brief free a metainfo */
void tr_metainfoFree(tr_info* inf);
