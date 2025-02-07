#include "datashard_active_transaction.h"
#include "datashard_ut_read_table.h"
#include <ydb/core/tx/data_events/payload_helper.h>
#include <ydb/core/tx/datashard/ut_common/datashard_ut_common.h>

namespace NKikimr {

using namespace NKikimr::NDataShard;
using namespace NSchemeShard;
using namespace Tests;
using namespace NDataShardReadTableTest;

Y_UNIT_TEST_SUITE(DataShardWrite) {
    const TString expectedTableState = "key = 0, value = 1\nkey = 2, value = 3\nkey = 4, value = 5\n";

    std::tuple<TTestActorRuntime&, Tests::TServer::TPtr, TActorId> TestCreateServer() {
        TPortManager pm;
        TServerSettings serverSettings(pm.GetPort(2134));
        serverSettings.SetDomainName("Root").SetUseRealThreads(false);

        Tests::TServer::TPtr server = new TServer(serverSettings);
        auto& runtime = *server->GetRuntime();
        auto sender = runtime.AllocateEdgeActor();

        runtime.SetLogPriority(NKikimrServices::TX_DATASHARD, NLog::PRI_TRACE);
        runtime.GetAppData().AllowReadTableImmediate = true;

        InitRoot(server, sender);

        return {runtime, server, sender};
    }

    Y_UNIT_TEST_TWIN(UpsertImmediate, EvWrite) {
        auto [runtime, server, sender] = TestCreateServer();

        auto opts = TShardedTableOptions();
        auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);

        auto rows = EvWrite ? TEvWriteRows{{{0, 1}}, {{2, 3}}, {{4, 5}}} : TEvWriteRows{};
        auto evWriteObservers = ReplaceEvProposeTransactionWithEvWrite(runtime, rows);

        Cout << "========= Send immediate write =========\n";
        {
            ExecSQL(server, sender, Q_("UPSERT INTO `/Root/table-1` (key, value) VALUES (0, 1);"));
            ExecSQL(server, sender, Q_("UPSERT INTO `/Root/table-1` (key, value) VALUES (2, 3);"));
            ExecSQL(server, sender, Q_("UPSERT INTO `/Root/table-1` (key, value) VALUES (4, 5);"));
        }

        Cout << "========= Read table =========\n";
        {
            auto tableState = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState, expectedTableState);
        }
    }

    Y_UNIT_TEST_TWIN(UpsertPrepared, EvWrite) {
        auto [runtime, server, sender] = TestCreateServer();

        // Disable volatile transactions, since EvWrite has not yet supported them.
        runtime.GetAppData().FeatureFlags.SetEnableDataShardVolatileTransactions(false);

        auto opts = TShardedTableOptions();
        auto [shards1, tableId1] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        auto [shards2, tableId2] = CreateShardedTable(server, sender, "/Root", "table-2", opts);

        auto rows = EvWrite ? TEvWriteRows{{tableId1, {0, 1}}, {tableId2, {2, 3}}} : TEvWriteRows{};
        auto evWriteObservers = ReplaceEvProposeTransactionWithEvWrite(runtime, rows);

        Cout << "========= Send distributed write =========\n";
        {
            ExecSQL(server, sender, Q_(
                "UPSERT INTO `/Root/table-1` (key, value) VALUES (0, 1);"
                "UPSERT INTO `/Root/table-2` (key, value) VALUES (2, 3);"));
        }

        Cout << "========= Read tables =========\n";
        {
            auto tableState1 = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            auto tableState2 = TReadTableState(server, MakeReadTableSettings("/Root/table-2")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState1, "key = 0, value = 1\n");
            UNIT_ASSERT_VALUES_EQUAL(tableState2, "key = 2, value = 3\n");
        }
    }

    Y_UNIT_TEST(WriteImmediate) {
        auto [runtime, server, sender] = TestCreateServer();

        auto opts = TShardedTableOptions().Columns({{"key", "Uint32", true, false}, {"value", "Uint32", false, false}});
        auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        const ui64 shard = shards[0];
        const ui32 rowCount = 3;

        ui64 txId = 100;

        Cout << "========= Send immediate write =========\n";
        {
            const auto writeResult = Write(runtime, sender, shard, tableId, opts.Columns_, rowCount, txId, NKikimrDataEvents::TEvWrite::MODE_IMMEDIATE);
            
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetOrigin(), shard);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetStep(), 0);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetOrderId(), txId);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetTxId(), txId);

            const auto& tableAccessStats = writeResult.GetTxStats().GetTableAccessStats(0);
            UNIT_ASSERT_VALUES_EQUAL(tableAccessStats.GetTableInfo().GetName(), "/Root/table-1");
            UNIT_ASSERT_VALUES_EQUAL(tableAccessStats.GetUpdateRow().GetCount(), rowCount);
        }

        Cout << "========= Read table =========\n";
        {
            auto tableState = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState, expectedTableState);
        }
    }

    Y_UNIT_TEST(WriteImmediateManyColumns) {
        auto [runtime, server, sender] = TestCreateServer();

        auto opts = TShardedTableOptions().Columns({{"key64", "Uint64", true, false}, {"key32", "Uint32", true, false},
                                                    {"value64", "Uint64", false, false}, {"value32", "Uint32", false, false}, {"valueUtf8", "Utf8", false, false}});
        auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        const ui64 shard = shards[0];
        const ui32 rowCount = 3;

        ui64 txId = 100;

        Cout << "========= Send immediate write =========\n";
        {
            Write(runtime, sender, shard, tableId, opts.Columns_, rowCount, txId, NKikimrDataEvents::TEvWrite::MODE_IMMEDIATE);
        }

        Cout << "========= Read table =========\n";
        {
            auto tableState = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState, "key64 = 0, key32 = 1, value64 = 2, value32 = 3, valueUtf8 = String_4\n"
                                                 "key64 = 5, key32 = 6, value64 = 7, value32 = 8, valueUtf8 = String_9\n"
                                                 "key64 = 10, key32 = 11, value64 = 12, value32 = 13, valueUtf8 = String_14\n");
        }
    }

    Y_UNIT_TEST(WriteImmediateHugeKey) {
        auto [runtime, server, sender] = TestCreateServer();

        auto opts = TShardedTableOptions().Columns({{"key", "Utf8", true, false}});
        auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        const ui64 shard = shards[0];

        Cout << "========= Send immediate write =========\n";
        {
            TString hugeStringValue(NLimits::MaxWriteKeySize + 1, 'X');
            TSerializedCellMatrix matrix({TCell(hugeStringValue.c_str(), hugeStringValue.size())}, 1, 1);

            auto evWrite = std::make_unique<NKikimr::NEvents::TDataEvents::TEvWrite>(100, NKikimrDataEvents::TEvWrite::MODE_IMMEDIATE);
            ui64 payloadIndex = NKikimr::NEvWrite::TPayloadWriter<NKikimr::NEvents::TDataEvents::TEvWrite>(*evWrite).AddDataToPayload(matrix.ReleaseBuffer());
            evWrite->AddOperation(NKikimrDataEvents::TEvWrite::TOperation::OPERATION_UPSERT, tableId, {1}, payloadIndex, NKikimrDataEvents::FORMAT_CELLVEC);

            const auto writeResult = Write(runtime, sender, shard, std::move(evWrite), NKikimrDataEvents::TEvWriteResult::STATUS_BAD_REQUEST);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetIssues().size(), 1);
            UNIT_ASSERT(writeResult.GetIssues(0).message().Contains("Row key size of 1049601 bytes is larger than the allowed threshold 1049600"));
        }
    }

    Y_UNIT_TEST(WritePrepared) {
        auto [runtime, server, sender] = TestCreateServer();

        TShardedTableOptions opts;
        const auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        const ui64 shard = shards[0];
        const ui64 coordinator = ChangeStateStorage(Coordinator, server->GetSettings().Domain);
        const ui32 rowCount = 3;

        ui64 txId = 100;
        ui64 minStep, maxStep;

        Cout << "========= Send prepare =========\n";
        {
            const auto writeResult = Write(runtime, sender, shard, tableId, opts.Columns_, rowCount, txId, NKikimrDataEvents::TEvWrite::MODE_PREPARE);

            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetStatus(), NKikimrDataEvents::TEvWriteResult::STATUS_PREPARED);
            UNIT_ASSERT_GT(writeResult.GetMinStep(), 0);
            UNIT_ASSERT_GT(writeResult.GetMaxStep(), writeResult.GetMinStep());
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetOrigin(), shard);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetTxId(), txId);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetDomainCoordinators().size(), 1);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetDomainCoordinators(0), coordinator);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetTabletInfo().GetTabletId(), shard);

            minStep = writeResult.GetMinStep();
            maxStep = writeResult.GetMaxStep();
        }

        Cout << "========= Send propose to coordinator =========\n";
        {
            SendProposeToCoordinator(server, shards, minStep, maxStep, txId);
        }

        Cout << "========= Wait for completed transaction =========\n";
        {
            auto writeResult = WaitForWriteCompleted(runtime, sender);

            UNIT_ASSERT_VALUES_EQUAL_C(writeResult.GetStatus(), NKikimrDataEvents::TEvWriteResult::STATUS_COMPLETED, "Status: " << writeResult.GetStatus() << " Issues: " << writeResult.GetIssues());
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetOrigin(), shard);
            UNIT_ASSERT_GE(writeResult.GetStep(), minStep);
            UNIT_ASSERT_LE(writeResult.GetStep(), maxStep);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetOrderId(), txId);
            UNIT_ASSERT_VALUES_EQUAL(writeResult.GetTxId(), txId);

            const auto& tableAccessStats = writeResult.GetTxStats().GetTableAccessStats(0);
            UNIT_ASSERT_VALUES_EQUAL(tableAccessStats.GetTableInfo().GetName(), "/Root/table-1");
            UNIT_ASSERT_VALUES_EQUAL(tableAccessStats.GetUpdateRow().GetCount(), rowCount);
        }

        Cout << "========= Read table =========\n";
        {
            auto tableState = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState, expectedTableState);
        }

    }
    
    Y_UNIT_TEST(WritePreparedNoTxCache) {
        auto [runtime, server, sender] = TestCreateServer();

        TShardedTableOptions opts;
        opts.DataTxCacheSize(0);  //disabling the tx cache for forced serialization/deserialization of txs
        const auto [shards, tableId] = CreateShardedTable(server, sender, "/Root", "table-1", opts);
        const ui64 shard = shards[0];
        const ui32 rowCount = 3;

        ui64 txId = 100;
        ui64 minStep, maxStep;

        Cout << "========= Send prepare =========\n";
        {
            const auto writeResult = Write(runtime, sender, shard, tableId, opts.Columns_, rowCount, txId, NKikimrDataEvents::TEvWrite::MODE_PREPARE);
            minStep = writeResult.GetMinStep();
            maxStep = writeResult.GetMaxStep();
        }

        Cout << "========= Send propose to coordinator =========\n";
        {
            SendProposeToCoordinator(server, shards, minStep, maxStep, txId);
        }

        Cout << "========= Wait for completed transaction =========\n";
        {
            WaitForWriteCompleted(runtime, sender);
        }

        Cout << "========= Read table =========\n";
        {
            auto tableState = TReadTableState(server, MakeReadTableSettings("/Root/table-1")).All();
            UNIT_ASSERT_VALUES_EQUAL(tableState, expectedTableState);
        }

    } // Y_UNIT_TEST

} // Y_UNIT_TEST_SUITE
} // namespace NKikimr