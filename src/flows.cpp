
#include "run_sp.h"
#include "flows.h"

#include "dgraph.h"
#include "heaps/heap_lib.h"

template <typename T>
void inst_graph (std::shared_ptr<DGraph> g, unsigned int nedges,
        const std::map <std::string, unsigned int>& vert_map,
        const std::vector <std::string>& from,
        const std::vector <std::string>& to,
        const std::vector <T>& dist,
        const std::vector <T>& wt)
{
    for (unsigned int i = 0; i < nedges; ++i)
    {
        unsigned int fromi = vert_map.at(from [i]);
        unsigned int toi = vert_map.at(to [i]);
        g->addNewEdge (fromi, toi, dist [i], wt [i]);
    }
}

struct OneFlow : public RcppParallel::Worker
{
    RcppParallel::RVector <int> dp_fromi;
    const std::vector <unsigned int> toi;
    const Rcpp::NumericMatrix flows;
    const std::vector <std::string> vert_name;
    const std::unordered_map <std::string, unsigned int> verts_to_edge_map;
    size_t nverts; // can't be const because of reinterpret cast
    size_t nedges;
    const double tol;
    const std::string dirtxt;
    const std::string heap_type;

    std::shared_ptr <DGraph> g;

    // constructor
    OneFlow (
            const Rcpp::IntegerVector fromi,
            const std::vector <unsigned int> toi_in,
            const Rcpp::NumericMatrix flows_in,
            const std::vector <std::string>  vert_name_in,
            const std::unordered_map <std::string, unsigned int> verts_to_edge_map_in,
            const size_t nverts_in,
            const size_t nedges_in,
            const double tol_in,
            const std::string dirtxt_in,
            const std::string &heap_type_in,
            const std::shared_ptr <DGraph> g_in) :
        dp_fromi (fromi), toi (toi_in), flows (flows_in), vert_name (vert_name_in),
        verts_to_edge_map (verts_to_edge_map_in),
        nverts (nverts_in), nedges (nedges_in), tol (tol_in),
        dirtxt (dirtxt_in), heap_type (heap_type_in), g (g_in)
    {
    }

    // Function to generate random file names
    std::string random_name(size_t len) {
        auto randchar = []() -> char
        {
            const char charset[] = \
               "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            //return charset [ rand() % max_index ];
            size_t i = static_cast <size_t> (floor (unif_rand () * max_index));
            return charset [i];
        }; // # nocov
        std::string str (len, 0);
        std::generate_n (str.begin(), len, randchar);
        return str;
    }

    // Parallel function operator
    void operator() (size_t begin, size_t end)
    {
        std::shared_ptr<PF::PathFinder> pathfinder =
            std::make_shared <PF::PathFinder> (nverts,
                    *run_sp::getHeapImpl (heap_type), g);
        std::vector <double> w (nverts);
        std::vector <double> d (nverts);
        std::vector <int> prev (nverts);

        std::vector <double> flowvec (nedges, 0.0);

        for (size_t i = begin; i < end; i++)
        {
            // These have to be reserved within the parallel operator function!
            std::fill (w.begin (), w.end (), INFINITE_DOUBLE);
            std::fill (d.begin (), d.end (), INFINITE_DOUBLE);

            unsigned int from_i = static_cast <unsigned int> (dp_fromi [i]);

            // reduce toi to only those within tolerance limt
            double fmax = 0.0;
            for (size_t j = 0; j < static_cast <size_t> (flows.ncol ()); j++)
                if (flows (i, j) > fmax)
                    fmax = flows (i, j);
            const double flim = fmax * tol;
            size_t nto = 0;
            for (size_t j = 0; j < static_cast <size_t> (flows.ncol ()); j++)
                if (flows (i, j) > flim)
                    nto++;

            std::vector <unsigned int> toi_reduced, toi_index;
            toi_reduced.reserve (nto);
            toi_index.reserve (nto);
            for (size_t j = 0; j < static_cast <size_t> (flows.ncol ()); j++)
                if (flows (i, j) > flim)
                {
                    toi_index.push_back (static_cast <unsigned int> (j));
                    toi_reduced.push_back (toi [j]);
                }

            pathfinder->Dijkstra (d, w, prev, from_i, toi_reduced);
            for (size_t j = 0; j < toi_reduced.size (); j++)
            {
                if (from_i != toi_reduced [j]) // Exclude self-flows
                {
                    double flow_ij = flows (i, toi_index [j]);
                    if (w [toi_reduced [j]] < INFINITE_DOUBLE && flow_ij > 0.0)
                    {
                        int target = static_cast <int> (toi_reduced [j]); // can equal -1
                        while (target < INFINITE_INT)
                        {
                            size_t stt = static_cast <size_t> (target);
                            if (prev [stt] >= 0 && prev [stt] < INFINITE_INT)
                            {
                                std::string v2 = "f" +
                                    vert_name [static_cast <size_t> (prev [stt])] +
                                    "t" + vert_name [stt];
                                // multiple flows can aggregate to same edge, so
                                // this has to be +=, not just =!
                                flowvec [verts_to_edge_map.at (v2)] += flow_ij;
                            }

                            target = static_cast <int> (prev [stt]);
                            // Only allocate that flow from origin vertex v to all
                            // previous vertices up until the target vi
                            if (target < 0 || target == dp_fromi [i])
                            {
                                break;
                            }
                        }
                    }
                }
            }
        } // end for i
        // dump flowvec to a file; chance of re-generating same file name is
        // 61^10, so there's no check for re-use of same
        std::string file_name = dirtxt + "_" + random_name (10) + ".dat";
        std::ofstream out_file;
        out_file.open (file_name, std::ios::binary | std::ios::out);
        out_file.write (reinterpret_cast <char *>(&nedges), sizeof (size_t));
        out_file.write (reinterpret_cast <char *>(&flowvec [0]),
                static_cast <std::streamsize> (nedges * sizeof (double)));
        out_file.close ();
    } // end parallel function operator
};


struct OneDisperse : public RcppParallel::Worker
{
    RcppParallel::RVector <int> dp_fromi;
    const Rcpp::NumericVector flows;
    const std::vector <std::string> vert_name;
    const std::unordered_map <std::string, unsigned int> verts_to_edge_map;
    size_t nverts; // can't be const because of reinterpret cast
    size_t nedges;
    const Rcpp::NumericVector k;
    const double tol;
    const std::string dirtxt;
    const std::string heap_type;

    std::shared_ptr <DGraph> g;

    // constructor
    OneDisperse (
            const Rcpp::IntegerVector fromi,
            const Rcpp::NumericVector flows_in,
            const std::vector <std::string>  vert_name_in,
            const std::unordered_map <std::string, unsigned int> verts_to_edge_map_in,
            const size_t nverts_in,
            const size_t nedges_in,
            const Rcpp::NumericVector k_in,
            const double tol_in,
            const std::string dirtxt_in,
            const std::string &heap_type_in,
            const std::shared_ptr <DGraph> g_in) :
        dp_fromi (fromi), flows (flows_in), vert_name (vert_name_in),
        verts_to_edge_map (verts_to_edge_map_in),
        nverts (nverts_in), nedges (nedges_in), k (k_in), tol (tol_in),
        dirtxt (dirtxt_in), heap_type (heap_type_in), g (g_in)
    {
    }

    // Function to generate random file names
    std::string random_name(size_t len) {
        auto randchar = []() -> char
        {
            const char charset[] = \
               "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            const size_t max_index = (sizeof(charset) - 1);
            //return charset [ rand() % max_index ];
            size_t i = static_cast <size_t> (floor (unif_rand () * max_index));
            return charset [i];
        }; // # nocov
        std::string str (len, 0);
        std::generate_n (str.begin(), len, randchar);
        return str;
    }

    // Parallel function operator
    void operator() (size_t begin, size_t end)
    {
        std::shared_ptr<PF::PathFinder> pathfinder =
            std::make_shared <PF::PathFinder> (nverts,
                    *run_sp::getHeapImpl (heap_type), g);
        std::vector <double> w (nverts);
        std::vector <double> d (nverts);
        std::vector <int> prev (nverts);

        std::vector <double> flowvec (nedges, 0.0);

        for (size_t i = begin; i < end; i++) // over the from vertices
        {
            R_xlen_t ir = static_cast <R_xlen_t> (i);
            // limit at which exp(-d/k) < tol
            const double dlim = -log10 (tol) * k [ir];

            std::fill (w.begin (), w.end (), INFINITE_DOUBLE);
            std::fill (d.begin (), d.end (), INFINITE_DOUBLE);

            const unsigned int from_i = static_cast <unsigned int> (dp_fromi [i]);

            pathfinder->DijkstraLimit (d, w, prev, from_i, dlim);
            for (size_t j = 0; j < nverts; j++)
            {
                if (prev [j] > 0)
                {
                    const std::string vert_to = vert_name [j],
                        vert_from = vert_name [static_cast <size_t> (prev [j])];
                    const std::string two_verts = "f" + vert_from + "t" + vert_to;

                    unsigned int indx = verts_to_edge_map.at (two_verts);
                    if (d [j] < INFINITE_DOUBLE)
                    {
                        if (k [ir] > 0.0)
                        {
                            flowvec [indx] += flows (i) * exp (-d [j] / k [ir]);
                        } else // standard logistic polynomial for UK cycling models
                        {
                            // # nocov start
                            double lp = -3.894 + (-0.5872 * d [j]) +
                                (1.832 * sqrt (d [j])) +
                                (0.007956 * d [j] * d [j]);
                            flowvec [indx] += flows (i) *
                                exp (lp) / (1.0 + exp (lp));
                            // # nocov end
                        }
                    }
                }
            }
        } // end for i
        // dump flowvec to a file; chance of re-generating same file name is
        // 61^10, so there's no check for re-use of same
        std::string file_name = dirtxt + "_" + random_name (10) + ".dat";
        std::ofstream out_file;
        out_file.open (file_name, std::ios::binary | std::ios::out);
        out_file.write (reinterpret_cast <char *>(&nedges), sizeof (size_t));
        out_file.write (reinterpret_cast <char *>(&flowvec [0]),
                static_cast <std::streamsize> (nedges * sizeof (double)));
        out_file.close ();
    } // end parallel function operator
};

//' rcpp_aggregate_files
//'
//' @param file_names List of fill names of files (that is, with path) provided
//' from R, coz otherwise this is C++17 with an added library flag.
//' @param len Length of flows, which is simply the number of edges in the
//' graph.
//'
//' Each parallel flow aggregation worker dumps results to a randomly-named
//' file. This routine reassembles those results into a single aggregate vector.
//'
//' @noRd
// [[Rcpp::export]]
Rcpp::NumericVector rcpp_aggregate_files (const Rcpp::CharacterVector file_names,
        const int len)
{
    Rcpp::NumericVector flows (len, 0.0);

    for (int i = 0; i < file_names.size (); i++)
    {
        size_t nedges;
        std::ifstream in_file (file_names [i], std::ios::binary | std::ios::in);
        in_file.read (reinterpret_cast <char *>(&nedges), sizeof (size_t));
        std::vector <double> flows_i (nedges);
        in_file.read (reinterpret_cast <char *>(&flows_i [0]),
                static_cast <std::streamsize> (nedges * sizeof (double)));
        in_file.close ();

        if (nedges != static_cast <size_t> (len))
            Rcpp::stop ("aggregate flows have inconsistent sizes"); // # nocov
        
        for (size_t j = 0; j < nedges; j++)
            flows [static_cast <long> (j)] += flows_i [j];
    }
    return flows;
}

//' rcpp_flows_aggregate_par
//'
//' @param graph The data.frame holding the graph edges
//' @param vert_map_in map from <std::string> vertex ID to (0-indexed) integer
//' index of vertices
//' @param fromi Index into vert_map_in of vertex numbers
//' @param toi Index into vert_map_in of vertex numbers
//' @param tol Relative tolerance in terms of flows below which targets
//' (to-vertices) are not considered.
//'
//' @note The parallelisation is achieved by dumping the results of each thread
//' to a file, with aggregation performed at the end by simply reading back and
//' aggregating all files. There is no way to aggregate into a single vector
//' because threads have to be independent. The only danger with this approach
//' is that multiple threads may generate the same file names, but with names 10
//' characters long, that chance should be 1 / 62 ^ 10.
//'
//' @noRd
// [[Rcpp::export]]
void rcpp_flows_aggregate_par (const Rcpp::DataFrame graph,
        const Rcpp::DataFrame vert_map_in,
        Rcpp::IntegerVector fromi,
        Rcpp::IntegerVector toi_in,
        Rcpp::NumericMatrix flows,
        const double tol,
        const std::string dirtxt,
        const std::string heap_type)
{
    std::vector <unsigned int> toi =
        Rcpp::as <std::vector <unsigned int> > ( toi_in);
    Rcpp::NumericVector id_vec;
    const size_t nfrom = static_cast <size_t> (fromi.size ());

    const std::vector <std::string> from = graph ["from"];
    const std::vector <std::string> to = graph ["to"];
    const std::vector <double> dist = graph ["d"];
    const std::vector <double> wt = graph ["w"];

    const unsigned int nedges = static_cast <unsigned int> (graph.nrow ());
    const std::vector <std::string> vert_name = vert_map_in ["vert"];
    const std::vector <unsigned int> vert_indx = vert_map_in ["id"];
    // Make map from vertex name to integer index
    std::map <std::string, unsigned int> vert_map_i;
    const size_t nverts = run_sp::make_vert_map (vert_map_in, vert_name,
            vert_indx, vert_map_i);

    std::unordered_map <std::string, unsigned int> verts_to_edge_map;
    std::unordered_map <std::string, double> verts_to_dist_map;
    run_sp::make_vert_to_edge_maps (from, to, wt, verts_to_edge_map, verts_to_dist_map);

    std::shared_ptr <DGraph> g = std::make_shared <DGraph> (nverts);
    inst_graph (g, nedges, vert_map_i, from, to, dist, wt);

    // Create parallel worker
    OneFlow one_flow (fromi, toi, flows, vert_name, verts_to_edge_map,
            nverts, nedges, tol, dirtxt, heap_type, g);

    GetRNGstate (); // Initialise R random seed
    RcppParallel::parallelFor (0, nfrom, one_flow);
    PutRNGstate ();
}



//' rcpp_flows_disperse_par
//'
//' Modified version of \code{rcpp_aggregate_flows} that aggregates flows to all
//' destinations from given set of origins, with flows attenuated by distance from
//' those origins.
//'
//' @param graph The data.frame holding the graph edges
//' @param vert_map_in map from <std::string> vertex ID to (0-indexed) integer
//' index of vertices
//' @param fromi Index into vert_map_in of vertex numbers
//' @param k Coefficient of (current proof-of-principle-only) exponential
//' distance decay function.  If value of \code{k<0} is given, a standard
//' logistic polynomial will be used.
//'
//' @note The flow data to be used for aggregation is a matrix mapping flows
//' betwen each pair of from and to points.
//'
//' @noRd
// [[Rcpp::export]]
void rcpp_flows_disperse_par (const Rcpp::DataFrame graph,
        const Rcpp::DataFrame vert_map_in,
        Rcpp::IntegerVector fromi,
        Rcpp::NumericVector k,
        Rcpp::NumericVector flows,
        const double &tol,
        const std::string &dirtxt,
        std::string heap_type)
{
    Rcpp::NumericVector id_vec;
    const size_t nfrom = static_cast <size_t> (fromi.size ());

    std::vector <std::string> from = graph ["from"];
    std::vector <std::string> to = graph ["to"];
    std::vector <double> dist = graph ["d"];
    std::vector <double> wt = graph ["w"];

    unsigned int nedges = static_cast <unsigned int> (graph.nrow ());
    std::vector <std::string> vert_name = vert_map_in ["vert"];
    std::vector <unsigned int> vert_indx = vert_map_in ["id"];
    // Make map from vertex name to integer index
    std::map <std::string, unsigned int> vert_map_i;
    size_t nverts = run_sp::make_vert_map (vert_map_in, vert_name,
            vert_indx, vert_map_i);

    std::unordered_map <std::string, unsigned int> verts_to_edge_map;
    std::unordered_map <std::string, double> verts_to_dist_map;
    run_sp::make_vert_to_edge_maps (from, to, wt, verts_to_edge_map, verts_to_dist_map);

    std::shared_ptr<DGraph> g = std::make_shared<DGraph> (nverts);
    inst_graph (g, nedges, vert_map_i, from, to, dist, wt);

    // Create parallel worker
    OneDisperse one_disperse (fromi, flows, vert_name, verts_to_edge_map,
            nverts, nedges, k, tol, dirtxt, heap_type, g);

    GetRNGstate (); // Initialise R random seed
    RcppParallel::parallelFor (0, nfrom, one_disperse);
    PutRNGstate ();
}
