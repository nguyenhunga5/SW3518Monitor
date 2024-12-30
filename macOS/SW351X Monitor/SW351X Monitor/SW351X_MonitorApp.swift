//
//  SW351X_MonitorApp.swift
//  SW351X Monitor
//
//  Created by Hung Nguyen Thanh on 28/12/24.
//

import SwiftUI
import SwiftData

@main
struct SW351X_MonitorApp: App {
    var sharedModelContainer: ModelContainer = {
        let schema = Schema([
            Item.self,
        ])
        let modelConfiguration = ModelConfiguration(schema: schema, isStoredInMemoryOnly: false)

        do {
            return try ModelContainer(for: schema, configurations: [modelConfiguration])
        } catch {
            fatalError("Could not create ModelContainer: \(error)")
        }
    }()

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .modelContainer(sharedModelContainer)
    }
}
