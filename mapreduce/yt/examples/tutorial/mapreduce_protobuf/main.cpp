#include <mapreduce/yt/interface/client.h>

#include <mapreduce/yt/examples/tutorial/mapreduce_protobuf/data.pb.h>

#include <util/stream/output.h>
#include <util/system/user.h>
#include <util/charset/utf8.h>

using namespace NYT;

//
// Для того чтобы запустить операцию mapreduce, нам нужны обычные классы Mapper'а и Reducer'а
// (эти классы даже можно использовать в других местах в отдельных операциях Map/Reduce).
//

class TNormalizeNameMapper
    : public IMapper<
          TTableReader<TLoginRecord>,
          TTableWriter<TLoginRecord>>
{
public:
    virtual void Do(TReader* reader, TWriter* writer) override
    {
        for (; reader->IsValid(); reader->Next()) {
            auto row = reader->GetRow();
            row.SetName(ToLowerUTF8(row.GetName()));
            writer->AddRow(row);
        }
    }
};
REGISTER_MAPPER(TNormalizeNameMapper);

class TCountNameReducer
    : public IReducer<
          TTableReader<TLoginRecord>,
          TTableWriter<TNameStatistics>>
{
public:
    virtual void Do(TReader* reader, TWriter* writer) override
    { TNameStatistics result;
        ui64 count = 0;
        for (; reader->IsValid(); reader->Next()) {
            const auto& row = reader->GetRow();
            if (!result.HasName()) {
                result.SetName(row.GetName());
            }
            ++count;
        }
        result.SetCount(count);
        writer->AddRow(result);
    }
};
REGISTER_REDUCER(TCountNameReducer);

int main(int argc, const char** argv) {
    NYT::Initialize(argc, argv);

    auto client = CreateClient("freud");

    const TString outputTable = "//tmp/" + GetUsername() + "-tutorial-output";

    // Запуск операции MapReduce несильно отличается от запуска других операций.
    // Нам надо указать список ключей, по которым мы будем редьюсить
    // и два класса -- один Mapper и один Reducer.
    client->MapReduce(
        TMapReduceOperationSpec()
            .ReduceBy({"name"})
            .AddInput<TLoginRecord>("//home/ermolovd/yt-tutorial/staff_unsorted")
            .AddOutput<TNameStatistics>(outputTable),
        new TNormalizeNameMapper,
        new TCountNameReducer);

    Cout << "Output table: https://yt.yandex-team.ru/freud/#page=navigation&offsetMode=row&path=" << outputTable << Endl;

    return 0;
}
