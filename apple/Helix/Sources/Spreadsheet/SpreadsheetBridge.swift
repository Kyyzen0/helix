import CxxStdlib
import Foundation
import HelixCore

/// Reads the attributes of a node out of the engine for the spreadsheet.
@MainActor
final class FacadeSpreadsheetSource: SpreadsheetSource {
    nonisolated init() {}

    func nodeName(_ node: UInt64) -> String? {
        helix.core.listNodes().first { $0.id == node }.map { String(engine: $0.name) }
    }

    func report(node: UInt64) -> Result<SpreadsheetReport, SpreadsheetFailure> {
        let report = helix.core.cookNodeReport(node)
        guard report.status.ok else { return .failure(SpreadsheetFailure(message: String(engineMessage: report.status.error))) }
        var result = SpreadsheetReport(
            generation: report.generation,
            pointCount: Int(clamping: report.pointCount),
            vertexCount: Int(clamping: report.vertexCount),
            faceCount: Int(clamping: report.faceCount),
            primitiveCount: Int(clamping: report.primitiveCount)
        )
        result.attributes = report.attributes.compactMap { info in
            guard let owner = Self.owner(info.owner) else { return nil }
            return SpreadsheetAttribute(
                owner: owner, name: String(engine: info.name), type: Self.type(info.type),
                componentCount: Int(info.componentCount), elementCount: Int(clamping: info.elementCount),
                mixedTypes: info.mixedTypes
            )
        }
        result.primitives = report.primitives.map {
            SpreadsheetPrimitiveRange(
                pointStart: Int(clamping: $0.pointStart), pointCount: Int(clamping: $0.pointCount),
                vertexStart: Int(clamping: $0.vertexStart), vertexCount: Int(clamping: $0.vertexCount),
                faceStart: Int(clamping: $0.faceStart), faceCount: Int(clamping: $0.faceCount)
            )
        }
        return .success(result)
    }

    func readPage(node: UInt64, owner: SpreadsheetOwner, attribute: String, start: Int, count: Int) -> SpreadsheetPage? {
        let values = helix.core.readAttribute(node, Self.facadeOwner(owner), std.string(attribute), UInt64(start), UInt64(count))
        guard values.status.ok else { return nil }
        return SpreadsheetPage(
            generation: values.generation,
            totalCount: Int(clamping: values.totalCount),
            componentCount: Int(values.componentCount),
            type: Self.type(values.type),
            floats: Array(values.floats),
            ints: Array(values.ints)
        )
    }

    private static func owner(_ owner: helix.core.AttributeOwner) -> SpreadsheetOwner? {
        switch owner {
        case .Point: .points
        case .Vertex: .vertices
        case .Face: .faces
        case .Primitive: .primitives
        @unknown default: nil
        }
    }

    private static func facadeOwner(_ owner: SpreadsheetOwner) -> helix.core.AttributeOwner {
        switch owner {
        case .points: .Point
        case .vertices: .Vertex
        case .faces: .Face
        case .primitives: .Primitive
        }
    }

    private static func type(_ type: helix.core.AttributeValueType) -> SpreadsheetValueType {
        switch type {
        case .Int: .int
        case .Float: .float
        case .Vector3: .vector3
        case .Bool: .bool
        case .Matrix4: .matrix4
        @unknown default: .float
        }
    }
}
