    ipc_on("proc.findExact", [](const json& a) -> json {
        const auto expected = U2W(a.value("path", std::string{}));
        if (expected.empty()) return json{{"found", false}, {"pid", 0}};
        const auto snapshot = ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),
            ymcc::processInventory::currentTime, 0);
        if (snapshot->status != ymcc::processInventory::Status::Complete)
            throw std::runtime_error("PROCESS_INVENTORY_UNAVAILABLE");
        for (const auto& entry : snapshot->rows) {
            const auto image = processImagePath(entry.pid);
            if (!image.empty() && sameFinalPath(image, expected))
                return json{{"found", true}, {"pid", static_cast<int>(entry.pid)}};
        }
        return json{{"found", false}, {"pid", 0}};
    });