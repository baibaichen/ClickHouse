#include <Interpreters/ProcessList.h>
#include <Processors/Port.h>
#include <QueryPipeline/BlockIO.h>
#include <QueryPipeline/printPipeline.h>
#include <Common/logger_useful.h>

namespace DB
{

void BlockIO::reset()
{
    /** process_list_entry should be destroyed after in, after out and after pipeline,
      *  since in, out and pipeline contain pointer to objects inside process_list_entry (query-level MemoryTracker for example),
      *  which could be used before destroying of in and out.
      *
      *  However, QueryStatus inside process_list_entry holds shared pointers to streams for some reason.
      *  Streams must be destroyed before storage locks, storages and contexts inside pipeline,
      *  so releaseQueryStreams() is required.
      */
    /// TODO simplify it all

    pipeline.reset();
    process_list_entry.reset();

    /// TODO Do we need also reset callbacks? In which order?
}

BlockIO & BlockIO::operator= (BlockIO && rhs) noexcept
{
    if (this == &rhs)
        return *this;

    /// Explicitly reset fields, so everything is destructed in right order
    reset();

    process_list_entry      = std::move(rhs.process_list_entry);
    pipeline                = std::move(rhs.pipeline);

    finish_callback         = std::move(rhs.finish_callback);
    exception_callback      = std::move(rhs.exception_callback);

    null_format             = rhs.null_format;

    return *this;
}

BlockIO::~BlockIO()
{
    reset();
}


static std::string dumpProcessor(const Processors & processors)
{
    for (const auto & processor : processors)
    {
        WriteBufferFromOwnString buffer;
        auto data_stats = processor->getProcessorDataStats();
        buffer << "(";
        buffer << "\nexecution time: " << processor->getElapsedNs() / 1000U << " us.";
        buffer << "\ninput wait time: " << processor->getInputWaitElapsedNs() / 1000U << " us.";
        buffer << "\noutput wait time: " << processor->getOutputWaitElapsedNs() / 1000U << " us.";
        buffer << "\ninput rows: " << data_stats.input_rows;
        buffer << "\ninput bytes: " << data_stats.input_bytes;
        buffer << "\noutput rows: " << data_stats.output_rows;
        buffer << "\noutput bytes: " << data_stats.output_bytes;
        buffer << ")";
        processor->setDescription(buffer.str());
    }
    WriteBufferFromOwnString out;
    printPipeline(processors, out);
    return out.str();
}

void BlockIO::onFinish(std::chrono::system_clock::time_point finish_time)
{
    LOG_TEST(&Poco::Logger::get("BlockIO"), "Pipeline finished: \n{}", dumpProcessor(pipeline.getProcessors()));

    if (finish_callback)
        finish_callback(std::move(pipeline), finish_time);
    else
        pipeline.reset();
}

void BlockIO::onException(bool log_as_error)
{
    setAllDataSent();

    if (exception_callback)
        exception_callback(log_as_error);

    pipeline.cancel();
    pipeline.reset();
}

void BlockIO::onCancelOrConnectionLoss()
{
    pipeline.cancel();
    pipeline.reset();
}

void BlockIO::setAllDataSent() const
{
    /// The following queries does not have process_list_entry:
    /// - internal
    /// - SHOW PROCESSLIST
    if (process_list_entry)
        process_list_entry->getQueryStatus()->setAllDataSent();
}


}
