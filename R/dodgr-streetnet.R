#' dodgr_streetnet
#'
#' Use the `osmdata` package to extract the street network for a given
#' location. For routing between a given set of points (passed as `pts`),
#' the `bbox` argument may be omitted, in which case a bounding box will
#' be constructed by expanding the range of `pts` by the relative amount of
#' `expand`.
#'
#' @param bbox Bounding box as vector or matrix of coordinates, or location
#' name. Passed to `osmdata::getbb`.
#' @param pts List of points presumably containing spatial coordinates
#' @param expand Relative factor by which street network should extend beyond
#' limits defined by pts (only if `bbox` not given).
#' @param quiet If `FALSE`, display progress messages
#' @return A Simple Features (`sf`) object with coordinates of all lines in
#' the street network.
#'
#' @export
#' @examples
#' \dontrun{
#' streetnet <- dodgr_streetnet ("hampi india", expand = 0)
#' # convert to form needed for `dodgr` functions:
#' graph <- weight_streetnet (streetnet)
#' nrow (graph) # 5,742 edges
#' # Alternative ways of extracting street networks by using a small selection of
#' # graph vertices to define bounding box:
#' verts <- dodgr_vertices (graph)
#' verts <- verts [sample (nrow (verts), size = 200), ]
#' streetnet <- dodgr_streetnet (pts = verts, expand = 0)
#' graph <- weight_streetnet (streetnet)
#' nrow (graph)
#' # This will generally have many more rows because most street networks include
#' # streets that extend considerably beyond the specified bounding box.
#'
#' # bbox can also be a polygon:
#' bb <- osmdata::getbb ("gent belgium") # rectangular bbox
#' nrow (dodgr_streetnet (bbox = bb)) # 28,742
#' bb <- osmdata::getbb ("gent belgium", format_out = "polygon")
#' nrow (dodgr_streetnet (bbox = bb)) # 15,969
#' # The latter has fewer rows because only edges within polygon are returned
#' }
dodgr_streetnet <- function (bbox, pts, expand = 0.05, quiet = TRUE)
{
    bbox_poly <- NULL
    if (!missing (bbox))
    {
        if (is.character (bbox))
            bbox <- osmdata::getbb (bbox)
        else if (is.list (bbox))
        {
            if (!all (vapply (bbox, is.numeric, logical (1))))
                stop ("bbox is a list, so items must be numeric ",
                      "(as in osmdata::getbb (..., format_out = 'polygon'))")
            (length (bbox) > 1)
                message ("selecting the first polygon from bbox")
            bbox_poly <- bbox [[1]]
            bbox <- t (apply (bbox [[1]], 2, range))
        } else if (is.numeric (bbox))
        {
            if (!inherits (bbox, "matrix"))
            {
                if (length (bbox) != 4)
                    stop ("bbox must have four numeric values")
                bbox <- rbind (sort (bbox [c (1, 3)]),
                             sort (bbox [c (2, 4)]))
            } else if (nrow (bbox) > 2)
            {
                bbox_poly <- bbox
                bbox <- apply (bbox, 2, range)
            }
        }
        bbox [1, ] <- bbox [1, ] + c (-expand, expand) * diff (bbox [1, ])
        bbox [2, ] <- bbox [2, ] + c (-expand, expand) * diff (bbox [2, ])
    } else if (!missing (pts))
    {
        nms <- names (pts)
        if (is.null (nms))
            nms <- colnames (pts)
        colx <- which (grepl ("x", nms, ignore.case = TRUE) |
                       grepl ("lon", nms, ignore.case = TRUE))
        coly <- which (grepl ("y", nms, ignore.case = TRUE) |
                       grepl ("lat", nms, ignore.case = TRUE))
        if (! (length (colx) == 1 | length (coly) == 1))
            stop ("Can not unambiguous determine coordinates in graph")

        x <- range (pts [, colx])
        x <- x + c (-expand, expand) * diff (x)
        y <- range (pts [, coly])
        y <- y + c (-expand, expand) * diff (y)

        bbox <- c (x [1], y [1], x [2], y [2])
    } else
        stop ('Either bbox or pts must be specified.')

    # osm_poly2line merges all street polygons with the line ones
    net <- osmdata::opq (bbox) %>%
                osmdata::add_osm_feature (key = "highway") %>%
                osmdata::osmdata_sf (quiet = quiet) %>%
                osmdata::osm_poly2line ()
    if (nrow (net$osm_lines) == 0)
        stop ("Street network unable to be downloaded")

    if (!is.null (bbox_poly))
        net <- osmdata::trim_osmdata (net, bbox_poly)

    return (net$osm_lines)
}