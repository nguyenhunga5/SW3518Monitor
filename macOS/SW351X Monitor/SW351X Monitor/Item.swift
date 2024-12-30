//
//  Item.swift
//  SW351X Monitor
//
//  Created by Hung Nguyen Thanh on 28/12/24.
//

import Foundation
import SwiftData

@Model
final class Item {
    var timestamp: Date!
    
    init(timestamp: Date) {
        self.timestamp = timestamp
    }
}
