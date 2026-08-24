// This file is part of the TrinityCore Project. See AUTHORS for copyright information.

using Evry.Playerbots;

HostApplicationBuilder builder = Host.CreateApplicationBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(options =>
{
    options.SingleLine = true;
    options.TimestampFormat = "yyyy-MM-dd HH:mm:ss ";
});
builder.Services.AddHostedService<CoordinatorWorker>();

IHost host = builder.Build();
await host.RunAsync();
